/*
 * bingo.c - A terminal bingo game using words from words.txt
 *
 * Compile: gcc -std=c11 -o bingo bingo.c
 * Run:     ./bingo
 *
 * Rules:
 *   - A 3x3 grid is filled with 9 random words from words.txt.
 *   - Each word has a child process spawned for it.
 *   - The user navigates a selection table with arrow keys / wasd,
 *     picking one word per column (9 picks total).
 *   - Every 60 seconds a funny insult is printed to remind the user to hurry.
 *   - When a word is selected its child is sent SIGTERM.
 *   - After all 9 picks, bingo winning conditions are checked.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

#define GRID_SIZE           9
#define GRID_DIM            3
#define MAX_WORDS           512
#define MAX_WORD_LEN        256
#define WORDS_FILE          "words.txt"
#define INSULT_INTERVAL_SEC 60
#define CHILD_MSG_LEN       128
#define SEL_COLS            9
#define MIN_COL_W           10
#define PAGE_SIZE           3

/* ------------------------------------------------------------------ */
/* Compile-time assertions                                            */
/* ------------------------------------------------------------------ */

_Static_assert(MAX_WORD_LEN > CHILD_MSG_LEN, 
    "MAX_WORD_LEN must be greater than CHILD_MSG_LEN to prevent truncation");
_Static_assert(GRID_SIZE == GRID_DIM * GRID_DIM,
    "GRID_SIZE must equal GRID_DIM * GRID_DIM for proper grid indexing");
_Static_assert(SEL_COLS % PAGE_SIZE == 0,
    "SEL_COLS must be evenly divisible by PAGE_SIZE for proper paging");

/* Layout calculation constants */
#define DUAL_GRID_PADDING       15
#define SINGLE_GRID_PADDING     6
#define MIN_CELL_WIDTH          8
#define MAX_CELL_WIDTH_DUAL     40
#define MAX_CELL_WIDTH_SINGLE   60
#define MAX_INNER_HEIGHT        4
#define TITLE_HEADER_OFFSET_1   60
#define TITLE_HEADER_OFFSET_2   56
#define TRUNCATION_ELLIPSIS_LEN 3
#define MIN_TRUNCATABLE_WIDTH   4

/* ANSI codes */
#define ANSI_RESET      "\033[0m"
#define ANSI_BOLD       "\033[1m"
#define ANSI_GREEN      "\033[1;32m"
#define ANSI_CYAN       "\033[1;36m"
#define ANSI_RED        "\033[1;31m"
#define ANSI_YELLOW     "\033[1;33m"
#define ANSI_BG_BLUE    "\033[44m"

/* Key codes */
#define KEY_UP          1
#define KEY_DOWN        2
#define KEY_LEFT        3
#define KEY_RIGHT       4
#define KEY_ENTER       5
#define KEY_OTHER       0

/* Default terminal dimensions */
#define DEFAULT_TERM_WIDTH  80
#define DEFAULT_TERM_HEIGHT 24

/* Child timing */
#define CHILD_MIN_SLEEP_SEC 3
#define CHILD_MAX_SLEEP_SEC 6

/* Status bar buffer size */
#define STATUS_BUFFER_SIZE  256

/* ------------------------------------------------------------------ */
/* Insults                                                             */
/* ------------------------------------------------------------------ */

static const char *insults[] = {
    "Still thinking? My grandma finishes bingo faster, and she's been dead for six years.",
    "At your current pace, the heat death of the universe will arrive before your second pick.",
    "I've seen faster decision-making from a Magic 8-Ball submerged in concrete.",
    "A golden retriever randomly slapping the keyboard would be quicker than you.",
    "Somewhere, a sloth is judging you. The sloth is winning.",
    "Carbon dating moves faster than your gameplay. Embarrassing.",
    "Geologists measure your response time in eras.",
    "The words aren't going to pick themselves. Or maybe they should, since you clearly can't.",
    "Even a stopped clock makes a decision twice a day. You: zero times so far.",
    "Scientists have confirmed that watching paint dry is more thrilling than watching you play.",
    "You're so slow, the child processes have filed for retirement benefits.",
    "Somewhere a tortoise is pointing and laughing.",
};
#define NUM_INSULTS (int)(sizeof(insults) / sizeof(insults[0]))

static const char *child_insults[] = {
    "I'm still running. You haven't caught me yet. Loser.",
    "Tick tock. I've aged more than your gameplay has progressed.",
    "My PID is still alive. Unlike your dignity.",
    "I exist. I taunt. You fumble. This is my purpose.",
    "I've been sitting here so long I'm getting existential. Pick already.",
    "I'm a background process doing more work than you are right now.",
    "You do know there's a word list right in front of you? Just checking.",
    "I've been running for so long I've written a memoir. It's mostly about your slowness.",
    "Somewhere a cron job is outperforming you.",
    "My parent process is disappointed in both of us.",
    "Even /dev/null is more decisive than you.",
    "I await SIGTERM with more courage than you approach this game.",
    "Fun fact: grep found your skill level. Results: 0 matches.",
    "At this rate, I'll die of old age before you kill me.",
    "I whisper your process name to the kernel. It laughs.",
};
#define NUM_CHILD_INSULTS (int)(sizeof(child_insults) / sizeof(child_insults[0]))

/* ------------------------------------------------------------------ */
/* Data Structures                                                     */
/* ------------------------------------------------------------------ */

/* Forward declarations for cleanup */
typedef struct game_state game_state_t;
typedef struct ui_state ui_state_t;
typedef struct process_state process_state_t;

/* Game configuration and word data */
typedef struct {
    char all_words[MAX_WORDS][MAX_WORD_LEN];
    int total_words;
    char grid[GRID_SIZE][MAX_WORD_LEN];
    int selected[GRID_SIZE];
} game_data_t;

/* Process management state */
typedef struct process_state {
    pid_t child_pids[GRID_SIZE];
    int pipe_fds[2];
    int pipe_read_open;
    int pipe_write_open;
} process_state_t;

/* UI and terminal state */
typedef struct ui_state {
    struct termios orig_termios;
    int terminal_restored;
    int raw_mode_enabled;
} ui_state_t;

/* Selection table state */
typedef struct {
    int col_heights[SEL_COLS];
    int col_confirmed[SEL_COLS];
    int col_choice[SEL_COLS];
    int cur_col;
    int cur_row;
} selection_state_t;

/* Complete game state */
typedef struct game_state {
    game_data_t data;
    process_state_t proc;
    ui_state_t ui;
    selection_state_t sel;
    volatile sig_atomic_t insult_tick;
    int insult_idx;
    char current_insult[CHILD_MSG_LEN];
} game_state_t;

/* Cleanup context for atexit */
typedef struct {
    game_state_t *state;
    int initialized;
} cleanup_context_t;

static cleanup_context_t g_cleanup_ctx = {NULL, 0};

/* ------------------------------------------------------------------ */
/* Signal Handling (Async-signal-safe)                                 */
/* ------------------------------------------------------------------ */

static volatile sig_atomic_t g_insult_tick = 0;

static void sigalrm_handler(int sig) {
    (void)sig;
    g_insult_tick = 1;
}

/* ------------------------------------------------------------------ */
/* Terminal Operations                                                 */
/* ------------------------------------------------------------------ */

static int restore_terminal(ui_state_t *ui) {
    if (!ui || !ui->raw_mode_enabled || ui->terminal_restored) {
        return 0;
    }
    
    printf("\033[?25h");
    fflush(stdout);
    
    if (tcsetattr(STDIN_FILENO, TCSANOW, &ui->orig_termios) < 0) {
        perror("tcsetattr");
        return -1;
    }
    
    ui->terminal_restored = 1;
    return 0;
}

static int enable_raw_mode(ui_state_t *ui) {
    if (!ui) return -1;
    
    if (tcgetattr(STDIN_FILENO, &ui->orig_termios) < 0) {
        perror("tcgetattr");
        return -1;
    }
    
    struct termios raw = ui->orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0) {
        perror("tcsetattr");
        return -1;
    }
    
    ui->raw_mode_enabled = 1;
    ui->terminal_restored = 0;
    return 0;
}

static void atexit_cleanup(void) {
    if (g_cleanup_ctx.initialized && g_cleanup_ctx.state) {
        restore_terminal(&g_cleanup_ctx.state->ui);
        
        /* Close any open pipe fds */
        if (g_cleanup_ctx.state->proc.pipe_fds[0] >= 0) {
            close(g_cleanup_ctx.state->proc.pipe_fds[0]);
        }
        if (g_cleanup_ctx.state->proc.pipe_fds[1] >= 0) {
            close(g_cleanup_ctx.state->proc.pipe_fds[1]);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Terminal Geometry                                                   */
/* ------------------------------------------------------------------ */

static int get_terminal_width(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return (int)ws.ws_col;
    }
    return DEFAULT_TERM_WIDTH;
}

static int get_terminal_height(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) {
        return (int)ws.ws_row;
    }
    return DEFAULT_TERM_HEIGHT;
}

/* ------------------------------------------------------------------ */
/* TUI Helpers                                                         */
/* ------------------------------------------------------------------ */

static int tui_move(int row, int col) {
    return printf("\033[%d;%dH", row, col);
}

static int tui_clear(void) {
    return printf("\033[2J\033[H\033[?25l");
}

static int cell_width_dual(void) {
    int cw = (get_terminal_width() - DUAL_GRID_PADDING) / 6;
    if (cw < MIN_CELL_WIDTH) cw = MIN_CELL_WIDTH;
    if (cw > MAX_CELL_WIDTH_DUAL) cw = MAX_CELL_WIDTH_DUAL;
    return cw;
}

static int cell_width_single(void) {
    int cw = (get_terminal_width() - SINGLE_GRID_PADDING) / 3;
    if (cw < MIN_CELL_WIDTH) cw = MIN_CELL_WIDTH;
    if (cw > MAX_CELL_WIDTH_SINGLE) cw = MAX_CELL_WIDTH_SINGLE;
    return cw;
}

static int cell_inner_height(int cw) {
    int h = cw / 4;
    if (h < 1) h = 1;
    if (h > MAX_INNER_HEIGHT) h = MAX_INNER_HEIGHT;
    return h;
}

/* ------------------------------------------------------------------ */
/* Grid Rendering                                                      */
/* ------------------------------------------------------------------ */

static int print_hrule(int cw) {
    int ret = 0;
    ret += printf("+");
    for (int c = 0; c < GRID_DIM; c++) {
        for (int i = 0; i < cw; i++) {
            ret += putchar('-');
        }
        ret += putchar('+');
    }
    return ret;
}

static int print_cell_word(const char *word, int cw, int highlight) {
    int ret = 0;
    char disp[MAX_WORD_LEN];
    
    if (!word || cw <= 0) return -1;
    
    strncpy(disp, word, sizeof(disp) - 1);
    disp[sizeof(disp) - 1] = '\0';
    
    if ((int)strlen(disp) > cw) {
        if (cw >= MIN_TRUNCATABLE_WIDTH) {
            disp[cw - TRUNCATION_ELLIPSIS_LEN] = '.';
            disp[cw - 2] = '.';
            disp[cw - 1] = '.';
        }
        disp[cw] = '\0';
    }
    
    int wlen = (int)strlen(disp);
    int lp = (cw - wlen) / 2;
    int rp = (cw - wlen) - lp;
    if (lp < 0) lp = 0;
    if (rp < 0) rp = 0;
    
    if (highlight) ret += printf(ANSI_GREEN ANSI_BOLD);
    ret += printf("%*s%s%*s", lp, "", disp, rp, "");
    if (highlight) ret += printf(ANSI_RESET);
    
    return ret;
}

/* ------------------------------------------------------------------ */
/* Word Loading                                                        */
/* ------------------------------------------------------------------ */

typedef enum {
    LOAD_SUCCESS = 0,
    LOAD_ERROR_FILE_NOT_FOUND = -1,
    LOAD_ERROR_TOO_MANY_WORDS = -2,
    LOAD_ERROR_READ_ERROR = -3,
    LOAD_ERROR_BUFFER_OVERFLOW = -4
} load_result_t;

static load_result_t load_words(game_data_t *data) {
    if (!data) return LOAD_ERROR_READ_ERROR;
    
    FILE *f = fopen(WORDS_FILE, "r");
    if (!f) {
        perror("fopen");
        return LOAD_ERROR_FILE_NOT_FOUND;
    }
    
    data->total_words = 0;
    char line[MAX_WORD_LEN];
    
    while (fgets(line, sizeof(line), f)) {
        if (data->total_words >= MAX_WORDS) {
            fprintf(stderr, "Warning: word list exceeds maximum of %d words, truncating\n", MAX_WORDS);
            fclose(f);
            return LOAD_ERROR_TOO_MANY_WORDS;
        }
        
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[--len] = '\0';
        }
        if (len == 0) continue;
        
        if (len >= MAX_WORD_LEN) {
            fprintf(stderr, "Warning: word truncated (too long)\n");
            line[MAX_WORD_LEN - 1] = '\0';
        }
        
        strncpy(data->all_words[data->total_words], line, MAX_WORD_LEN - 1);
        data->all_words[data->total_words][MAX_WORD_LEN - 1] = '\0';
        data->total_words++;
    }
    
    if (ferror(f)) {
        perror("fgets");
        fclose(f);
        return LOAD_ERROR_READ_ERROR;
    }
    
    fclose(f);
    return LOAD_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Shuffle & Grid Fill                                                 */
/* ------------------------------------------------------------------ */

static void shuffle(int *arr, int n) {
    if (!arr || n <= 0) return;
    
    for (int i = n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

static int fill_grid(game_data_t *data) {
    if (!data || data->total_words < GRID_SIZE) return -1;
    
    int *indices = malloc(data->total_words * sizeof(int));
    if (!indices) {
        perror("malloc");
        return -1;
    }
    
    for (int i = 0; i < data->total_words; i++) {
        indices[i] = i;
    }
    
    shuffle(indices, data->total_words);
    
    for (int i = 0; i < GRID_SIZE; i++) {
        strncpy(data->grid[i], data->all_words[indices[i]], MAX_WORD_LEN - 1);
        data->grid[i][MAX_WORD_LEN - 1] = '\0';
    }
    
    free(indices);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Child Process Behavior                                              */
/* ------------------------------------------------------------------ */

static void child_main(int index, int write_fd) {
    srand((unsigned)time(NULL) ^ (unsigned)getpid() ^ (unsigned)index);
    
    while (1) {
        sleep(CHILD_MIN_SLEEP_SEC + rand() % (CHILD_MAX_SLEEP_SEC - CHILD_MIN_SLEEP_SEC + 1));
        
        const char *ins = child_insults[rand() % NUM_CHILD_INSULTS];
        char msg[CHILD_MSG_LEN];
        
        int ret = snprintf(msg, sizeof(msg), "[Process %d] %s", index + 1, ins);
        if (ret < 0 || (size_t)ret >= sizeof(msg)) {
            /* Truncation occurred, handle gracefully */
            msg[sizeof(msg) - 1] = '\0';
        }
        
        size_t len = strlen(msg);
        if (len < CHILD_MSG_LEN - 1) {
            memset(msg + len, 0, CHILD_MSG_LEN - len);
        }
        
        if (write(write_fd, msg, CHILD_MSG_LEN) < 0) {
            break;
        }
    }
    
    exit(0);
}

/* ------------------------------------------------------------------ */
/* Process Management                                                  */
/* ------------------------------------------------------------------ */

typedef enum {
    PROC_SUCCESS = 0,
    PROC_ERROR_PIPE = -1,
    PROC_ERROR_FORK = -2,
    PROC_ERROR_FCNTL = -3
} proc_result_t;

static void init_process_state(process_state_t *proc) {
    if (!proc) return;
    
    memset(proc, 0, sizeof(*proc));
    proc->pipe_fds[0] = -1;
    proc->pipe_fds[1] = -1;
    proc->pipe_read_open = 0;
    proc->pipe_write_open = 0;
    
    for (int i = 0; i < GRID_SIZE; i++) {
        proc->child_pids[i] = -1;
    }
}

static void cleanup_children(process_state_t *proc, int up_to_index) {
    if (!proc) return;
    
    for (int j = 0; j < up_to_index && j < GRID_SIZE; j++) {
        if (proc->child_pids[j] > 0) {
            kill(proc->child_pids[j], SIGKILL);
            waitpid(proc->child_pids[j], NULL, 0);
            proc->child_pids[j] = -1;
        }
    }
}

static proc_result_t create_pipe(process_state_t *proc) {
    if (!proc) return PROC_ERROR_PIPE;
    
    if (pipe(proc->pipe_fds) < 0) {
        perror("pipe");
        return PROC_ERROR_PIPE;
    }
    
    proc->pipe_read_open = 1;
    proc->pipe_write_open = 1;
    return PROC_SUCCESS;
}

static proc_result_t spawn_children(process_state_t *proc) {
    if (!proc || !proc->pipe_read_open) return PROC_ERROR_PIPE;
    
    for (int i = 0; i < GRID_SIZE; i++) {
        pid_t pid = fork();
        
        if (pid < 0) {
            perror("fork");
            cleanup_children(proc, i);
            return PROC_ERROR_FORK;
        }
        
        if (pid == 0) {
            /* Child process */
            close(proc->pipe_fds[0]);
            child_main(i, proc->pipe_fds[1]);
            exit(0);
        }
        
        proc->child_pids[i] = pid;
    }
    
    return PROC_SUCCESS;
}

static proc_result_t setup_parent_pipe(process_state_t *proc) {
    if (!proc || !proc->pipe_write_open) return PROC_ERROR_PIPE;
    
    close(proc->pipe_fds[1]);
    proc->pipe_fds[1] = -1;
    proc->pipe_write_open = 0;
    
    if (fcntl(proc->pipe_fds[0], F_SETFL, O_NONBLOCK) < 0) {
        perror("fcntl");
        return PROC_ERROR_FCNTL;
    }
    
    return PROC_SUCCESS;
}

static void kill_all_children(process_state_t *proc) {
    if (!proc) return;

    for (int i = 0; i < GRID_SIZE; i++) {
        if (proc->child_pids[i] > 0) {
            kill(proc->child_pids[i], SIGTERM);
        }
    }
}

static void reap_all_children(process_state_t *proc) {
    if (!proc) return;
    
    for (int i = 0; i < GRID_SIZE; i++) {
        if (proc->child_pids[i] > 0) {
            waitpid(proc->child_pids[i], NULL, 0);
            proc->child_pids[i] = -1;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Signal Setup                                                        */
/* ------------------------------------------------------------------ */

static int setup_sigalrm(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigalrm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    
    if (sigaction(SIGALRM, &sa, NULL) < 0) {
        perror("sigaction");
        return -1;
    }
    
    return 0;
}

/* ------------------------------------------------------------------ */
/* Selection Table Helpers                                             */
/* ------------------------------------------------------------------ */

static void init_selection_state(selection_state_t *sel, int total_words) {
    if (!sel) return;
    
    memset(sel, 0, sizeof(*sel));
    
    int base = total_words / SEL_COLS;
    int rem = total_words % SEL_COLS;
    
    for (int c = 0; c < SEL_COLS; c++) {
        sel->col_heights[c] = base + (c < rem ? 1 : 0);
    }
}

static int count_confirmed(const selection_state_t *sel) {
    if (!sel) return 0;
    
    int done = 0;
    for (int c = 0; c < SEL_COLS; c++) {
        if (sel->col_confirmed[c]) done++;
    }
    return done;
}

static int is_selection_complete(const selection_state_t *sel) {
    return count_confirmed(sel) == SEL_COLS;
}

static void handle_key_input(selection_state_t *sel, int key) {
    if (!sel) return;
    
    switch (key) {
        case KEY_LEFT:
            sel->cur_col = (sel->cur_col - 1 + SEL_COLS) % SEL_COLS;
            sel->cur_row = 0;
            break;
        case KEY_RIGHT:
            sel->cur_col = (sel->cur_col + 1) % SEL_COLS;
            sel->cur_row = 0;
            break;
        case KEY_UP:
            if (sel->cur_row > 0) sel->cur_row--;
            break;
        case KEY_DOWN:
            if (sel->cur_row < sel->col_heights[sel->cur_col] - 1) sel->cur_row++;
            break;
        case KEY_ENTER:
            sel->col_confirmed[sel->cur_col] = 1;
            sel->col_choice[sel->cur_col] = sel->cur_row;
            break;
    }
}

static void collect_picks(const game_data_t *data, const selection_state_t *sel, 
                         char picked[][MAX_WORD_LEN]) {
    if (!data || !sel || !picked) return;
    
    int base = 0;
    for (int c = 0; c < SEL_COLS; c++) {
        int wi = base + sel->col_choice[c];
        strncpy(picked[c], data->all_words[wi], MAX_WORD_LEN - 1);
        picked[c][MAX_WORD_LEN - 1] = '\0';
        base += sel->col_heights[c];
    }
}

/* ------------------------------------------------------------------ */
/* Word List Shuffling                                                 */
/* ------------------------------------------------------------------ */

static int shuffle_word_list(game_data_t *data) {
    if (!data || data->total_words <= 0) return -1;
    
    /* BUG: off-by-one — allocates one int too few; Electric Fence catches the
     * out-of-bounds write when shuffle() accesses indices[total_words - 1]. */
    int *indices = malloc((data->total_words - 1) * sizeof(int));
    if (!indices) {
        perror("malloc");
        return -1;
    }
    
    for (int i = 0; i < data->total_words; i++) {
        indices[i] = i;
    }
    
    shuffle(indices, data->total_words);
    
    char **tmp_words = malloc(data->total_words * sizeof(char*));
    if (!tmp_words) {
        perror("malloc");
        free(indices);
        return -1;
    }
    
    for (int i = 0; i < data->total_words; i++) {
        tmp_words[i] = malloc(MAX_WORD_LEN);
        if (!tmp_words[i]) {
            for (int j = 0; j < i; j++) free(tmp_words[j]);
            free(tmp_words);
            free(indices);
            return -1;
        }
        strncpy(tmp_words[i], data->all_words[indices[i]], MAX_WORD_LEN - 1);
        tmp_words[i][MAX_WORD_LEN - 1] = '\0';
    }
    
    for (int i = 0; i < data->total_words; i++) {
        strncpy(data->all_words[i], tmp_words[i], MAX_WORD_LEN - 1);
        data->all_words[i][MAX_WORD_LEN - 1] = '\0';
        free(tmp_words[i]);
    }
    
    free(tmp_words);
    free(indices);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Hidden Grid Display                                                 */
/* ------------------------------------------------------------------ */

static int print_hidden_grid(void) {
    int tw = get_terminal_width();
    int th = get_terminal_height();
    int cw = cell_width_single();
    int inner_h = cell_inner_height(cw);
    int word_line = inner_h / 2;
    int gw = GRID_DIM * cw + GRID_DIM + 1;
    int margin = (tw - gw) / 2;
    if (margin < 0) margin = 0;
    
    int grid_lines = GRID_DIM * (inner_h + 1) + 1;
    int used = 1 + 1 + grid_lines + 2;
    int top_pad = (th - used) / 2;
    if (top_pad < 0) top_pad = 0;
    
    if (tui_clear() < 0) return -1;
    if (tui_move(top_pad + 1, 1) < 0) return -1;
    
    const char *title = "=== BINGO ===";
    int tl = (tw - (int)strlen(title)) / 2;
    if (tl < 0) tl = 0;
    
    int ret = 0;
    ret += printf("%*s" ANSI_BOLD ANSI_CYAN "%s" ANSI_RESET "\n\n", tl, "", title);
    
    for (int r = 0; r < GRID_DIM; r++) {
        ret += printf("%*s", margin, "");
        ret += print_hrule(cw);
        ret += printf("\n");
        
        for (int l = 0; l < inner_h; l++) {
            ret += printf("%*s|", margin, "");
            for (int c = 0; c < GRID_DIM; c++) {
                if (l == word_line) {
                    ret += print_cell_word("[ ? ]", cw, 0);
                } else {
                    ret += printf("%*s", cw, "");
                }
                ret += putchar('|');
            }
            ret += printf("\n");
        }
    }
    
    ret += printf("%*s", margin, "");
    ret += print_hrule(cw);
    ret += printf("\n");
    
    const char *sub = "9 words hidden  \xe2\x80\x94  revealed at the end";
    int sl = (tw - (int)strlen(sub)) / 2;
    if (sl < 0) sl = 0;
    ret += printf("\n%*s" ANSI_CYAN "%s" ANSI_RESET "\n", sl, "", sub);
    
    const char *prompt = "Press any key to start selection...";
    int pl = (tw - (int)strlen(prompt)) / 2;
    if (pl < 0) pl = 0;
    if (tui_move(th, 1) < 0) return -1;
    ret += printf("%*s" ANSI_YELLOW "%s" ANSI_RESET, pl, "", prompt);
    
    if (fflush(stdout) != 0) {
        perror("fflush");
        return -1;
    }
    
    return ret;
}

/* ------------------------------------------------------------------ */
/* Final Result Display                                                */
/* ------------------------------------------------------------------ */

static void mark_matched_picks(const char picked[][MAX_WORD_LEN], 
                               const game_data_t *data, int *pick_matched) {
    if (!picked || !data || !pick_matched) return;
    
    for (int p = 0; p < SEL_COLS; p++) {
        pick_matched[p] = 0;
        for (int g = 0; g < GRID_SIZE; g++) {
            if (strcmp(picked[p], data->grid[g]) == 0) {
                pick_matched[p] = 1;
                break;
            }
        }
    }
}

static int print_final_result(const char picked[][MAX_WORD_LEN], 
                              const game_data_t *data) {
    int tw = get_terminal_width();
    int th = get_terminal_height();
    int cw = cell_width_dual();
    int inner_h = cell_inner_height(cw);
    int word_line = inner_h / 2;
    int gw = GRID_DIM * cw + GRID_DIM + 1;
    int gap = tw - 2 * gw;
    if (gap < 3) gap = 3;
    int left_off = (tw - (2 * gw + gap)) / 2;
    if (left_off < 0) left_off = 0;
    
    int pick_matched[SEL_COLS];
    mark_matched_picks(picked, data, pick_matched);
    
    int grid_lines = GRID_DIM * (inner_h + 1) + 1;
    int used = 1 + 1 + 1 + 1 + grid_lines;
    int top_pad = (th - used) / 2;
    if (top_pad < 1) top_pad = 1;
    
    if (tui_clear() < 0) return -1;
    if (tui_move(top_pad, 1) < 0) return -1;
    
    const char *title = "=== FINAL RESULT ===";
    int tl = (tw - (int)strlen(title)) / 2;
    if (tl < 0) tl = 0;
    
    int ret = 0;
    ret += printf("%*s" ANSI_BOLD "%s" ANSI_RESET "\n\n", tl, "", title);
    
    const char *lhdr = "YOUR PICKS";
    const char *rhdr = "BINGO GRID";
    int ll = (gw - (int)strlen(lhdr)) / 2;
    int rl = (gw - (int)strlen(rhdr)) / 2;
    if (ll < 0) ll = 0;
    if (rl < 0) rl = 0;
    
    ret += printf("%*s%*s" ANSI_BOLD ANSI_CYAN "%s" ANSI_RESET "%*s",
                  left_off, "", ll, "", lhdr, gw - ll - (int)strlen(lhdr), "");
    ret += printf("%*s", gap, "");
    ret += printf("%*s" ANSI_BOLD ANSI_CYAN "%s" ANSI_RESET "\n\n",
                  rl, "", rhdr);
    
    for (int r = 0; r < GRID_DIM; r++) {
        ret += printf("%*s", left_off, "");
        ret += print_hrule(cw);
        ret += printf("%*s", gap, "");
        ret += print_hrule(cw);
        ret += printf("\n");
        
        for (int l = 0; l < inner_h; l++) {
            ret += printf("%*s|", left_off, "");
            for (int c = 0; c < GRID_DIM; c++) {
                int pi = r * GRID_DIM + c;
                if (l == word_line) {
                    ret += print_cell_word(picked[pi], cw, pick_matched[pi]);
                } else {
                    ret += printf("%*s", cw, "");
                }
                ret += putchar('|');
            }
            ret += printf("%*s|", gap, "");
            for (int c = 0; c < GRID_DIM; c++) {
                int gi = r * GRID_DIM + c;
                if (l == word_line) {
                    ret += print_cell_word(data->grid[gi], cw, data->selected[gi]);
                } else {
                    ret += printf("%*s", cw, "");
                }
                ret += putchar('|');
            }
            ret += printf("\n");
        }
    }
    
    ret += printf("%*s", left_off, "");
    ret += print_hrule(cw);
    ret += printf("%*s", gap, "");
    ret += print_hrule(cw);
    ret += printf("\n");
    
    if (fflush(stdout) != 0) {
        perror("fflush");
        return -1;
    }
    
    return ret;
}

/* ------------------------------------------------------------------ */
/* Selection Table Display                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    int use_pages;
    int col_start;
    int col_end;
    int col_w;
    int vis_cols;
    int max_rows;
    int top_pad;
    int left_margin;
} layout_info_t;

static void calculate_layout(const selection_state_t *sel, layout_info_t *layout) {
    if (!sel || !layout) return;
    
    int tw = get_terminal_width();
    int th = get_terminal_height();
    
    int full_col_w = (tw - 2 - (SEL_COLS - 1)) / SEL_COLS;
    layout->use_pages = (full_col_w < MIN_COL_W);
    
    if (layout->use_pages) {
        int page = sel->cur_col / PAGE_SIZE;
        layout->col_start = page * PAGE_SIZE;
        layout->col_end = layout->col_start + PAGE_SIZE - 1;
        if (layout->col_end >= SEL_COLS) layout->col_end = SEL_COLS - 1;
        int n = layout->col_end - layout->col_start + 1;
        layout->col_w = (tw - 2 - (n - 1)) / n;
    } else {
        layout->col_start = 0;
        layout->col_end = SEL_COLS - 1;
        layout->col_w = full_col_w;
    }
    
    if (layout->col_w < MIN_TRUNCATABLE_WIDTH) layout->col_w = MIN_TRUNCATABLE_WIDTH;
    
    layout->vis_cols = layout->col_end - layout->col_start + 1;
    layout->max_rows = 0;
    for (int c = layout->col_start; c <= layout->col_end; c++) {
        if (sel->col_heights[c] > layout->max_rows) {
            layout->max_rows = sel->col_heights[c];
        }
    }
    
    int content_lines = 1 + 1 + 1 + 1 + layout->max_rows + 1;
    layout->top_pad = (th - 1 - content_lines) / 2;
    if (layout->top_pad < 0) layout->top_pad = 0;
    
    int table_w = layout->vis_cols * layout->col_w + (layout->vis_cols - 1);
    layout->left_margin = (tw - table_w) / 2;
    if (layout->left_margin < 0) layout->left_margin = 0;
}

static int print_table_header(const layout_info_t *layout, const selection_state_t *sel) {
    if (!layout || !sel) return -1;
    
    int tw = get_terminal_width();
    int ret = 0;
    
    if (layout->use_pages) {
        int page = sel->cur_col / PAGE_SIZE;
        int hl = (tw - TITLE_HEADER_OFFSET_1) / 2;
        if (hl < 0) hl = 0;
        ret += printf("%*s" ANSI_BOLD "Select one word per column  "
                      ANSI_CYAN "(wasd/arrows | ENTER=confirm)  "
                      ANSI_YELLOW "[Page %d/%d: cols %d-%d]"
                      ANSI_RESET "\n\n",
                      hl, "", page + 1, SEL_COLS / PAGE_SIZE, 
                      layout->col_start + 1, layout->col_end + 1);
    } else {
        int hl = (tw - TITLE_HEADER_OFFSET_2) / 2;
        if (hl < 0) hl = 0;
        ret += printf("%*s" ANSI_BOLD "Select one word per column  "
                      ANSI_CYAN "(wasd / arrows  |  ENTER=confirm column)"
                      ANSI_RESET "\n\n", hl, "");
    }
    
    return ret;
}

static int print_column_headers(const layout_info_t *layout) {
    if (!layout) return -1;
    
    int ret = printf("%*s", layout->left_margin, "");
    
    for (int c = layout->col_start; c <= layout->col_end; c++) {
        char hdr[16];
        int n = snprintf(hdr, sizeof(hdr), "Col %d", c + 1);
        if (n < 0 || (size_t)n >= sizeof(hdr)) {
            strncpy(hdr, "Col ?", sizeof(hdr) - 1);
            hdr[sizeof(hdr) - 1] = '\0';
        }
        
        int space = layout->col_w - (int)strlen(hdr);
        if (space < 0) space = 0;
        ret += printf(ANSI_BOLD "%s%*s" ANSI_RESET, hdr, space, "");
        if (c < layout->col_end) ret += printf(" ");
    }
    ret += printf("\n");
    
    return ret;
}

static int print_separator(const layout_info_t *layout) {
    if (!layout) return -1;
    
    int ret = printf("%*s", layout->left_margin, "");
    for (int c = layout->col_start; c <= layout->col_end; c++) {
        for (int i = 0; i < layout->col_w; i++) {
            ret += printf("-");
        }
        if (c < layout->col_end) ret += printf(" ");
    }
    ret += printf("\n");
    
    return ret;
}

static int print_table_row(const layout_info_t *layout, const selection_state_t *sel,
                           const game_data_t *data, int r) {
    if (!layout || !sel || !data) return -1;
    
    int ret = printf("%*s", layout->left_margin, "");
    
    for (int c = layout->col_start; c <= layout->col_end; c++) {
        int base = 0;
        for (int x = 0; x < c; x++) base += sel->col_heights[x];
        int wi = base + r;
        
        if (r >= sel->col_heights[c]) {
            ret += printf("%-*s", layout->col_w, "");
            if (c < layout->col_end) ret += printf(" ");
            continue;
        }
        
        int is_cursor = (c == sel->cur_col && r == sel->cur_row);
        int is_confirmed = (sel->col_confirmed[c] && sel->col_choice[c] == r);
        
        if (is_cursor) ret += printf(ANSI_BG_BLUE ANSI_BOLD);
        else if (is_confirmed) ret += printf(ANSI_GREEN ANSI_BOLD);
        
        char display[MAX_WORD_LEN];
        strncpy(display, data->all_words[wi], sizeof(display) - 1);
        display[sizeof(display) - 1] = '\0';
        
        if ((int)strlen(display) > layout->col_w) {
            if (layout->col_w >= MIN_TRUNCATABLE_WIDTH) {
                display[layout->col_w - TRUNCATION_ELLIPSIS_LEN] = '.';
                display[layout->col_w - 2] = '.';
                display[layout->col_w - 1] = '.';
            }
            display[layout->col_w] = '\0';
        }
        
        ret += printf("%-*s", layout->col_w, display);
        
        if (is_cursor || is_confirmed) ret += printf(ANSI_RESET);
        if (c < layout->col_end) ret += printf(" ");
    }
    ret += printf("\n");
    
    return ret;
}

static int print_status_bar(int done) {
    int tw = get_terminal_width();
    char status[STATUS_BUFFER_SIZE];
    int n = snprintf(status, sizeof(status), "Confirmed: %d / %d", done, SEL_COLS);
    if (n < 0 || (size_t)n >= sizeof(status)) {
        strncpy(status, "Status unknown", sizeof(status) - 1);
        status[sizeof(status) - 1] = '\0';
    }
    
    int sl = (tw - (int)strlen(status)) / 2;
    if (sl < 0) sl = 0;
    return printf("\n%*s" ANSI_YELLOW "%s" ANSI_RESET "\n", sl, "", status);
}

static int print_footer(const layout_info_t *layout, const char *insult_msg) {
    if (!layout) return -1;
    
    int th = get_terminal_height();
    int tw = get_terminal_width();
    int ret = 0;
    
    if (tui_move(th, 1) < 0) return -1;
    
    if (insult_msg && insult_msg[0]) {
        int il = (tw - (int)strlen(insult_msg) - 4) / 2;
        if (il < 0) il = 0;
        ret += printf("%*s" ANSI_RED ANSI_BOLD ">> %s <<" ANSI_RESET, 
                      il, "", insult_msg);
    } else {
        const char *hint = layout->use_pages
            ? "A/D: switch page  W/S: pick word  ENTER: confirm"
            : "A/D: change column  W/S: pick word  ENTER: confirm";
        int hl = (tw - (int)strlen(hint)) / 2;
        if (hl < 0) hl = 0;
        ret += printf("%*s" ANSI_CYAN "%s" ANSI_RESET, hl, "", hint);
    }
    
    return ret;
}

static int print_selection_table(const game_state_t *state) {
    if (!state) return -1;
    
    const selection_state_t *sel = &state->sel;
    const game_data_t *data = &state->data;
    const char *insult_msg = state->current_insult;
    
    layout_info_t layout;
    calculate_layout(sel, &layout);
    
    if (tui_clear() < 0) return -1;
    if (tui_move(layout.top_pad + 1, 1) < 0) return -1;
    
    int ret = 0;
    ret += print_table_header(&layout, sel);
    ret += print_column_headers(&layout);
    ret += print_separator(&layout);
    
    for (int r = 0; r < layout.max_rows; r++) {
        ret += print_table_row(&layout, sel, data, r);
    }
    
    int done = count_confirmed(sel);
    ret += print_status_bar(done);
    ret += print_footer(&layout, insult_msg);
    
    if (fflush(stdout) != 0) {
        perror("fflush");
        return -1;
    }
    
    return ret;
}

/* ------------------------------------------------------------------ */
/* Input Handling                                                      */
/* ------------------------------------------------------------------ */

static int read_key(void) {
    unsigned char c;
    
    if (read(STDIN_FILENO, &c, 1) != 1) return KEY_OTHER;
    
    if (c == '\r' || c == '\n') return KEY_ENTER;
    
    if (c == 0x1b) {
        unsigned char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return KEY_OTHER;
        if (seq[0] == '[') {
            if (read(STDIN_FILENO, &seq[1], 1) != 1) return KEY_OTHER;
            switch (seq[1]) {
                case 'A': return KEY_UP;
                case 'B': return KEY_DOWN;
                case 'C': return KEY_RIGHT;
                case 'D': return KEY_LEFT;
            }
        }
        return KEY_OTHER;
    }
    
    switch (c) {
        case 'w': case 'W': return KEY_UP;
        case 's': case 'S': return KEY_DOWN;
        case 'a': case 'A': return KEY_LEFT;
        case 'd': case 'D': return KEY_RIGHT;
    }
    
    return KEY_OTHER;
}

/* ------------------------------------------------------------------ */
/* Game Logic                                                          */
/* ------------------------------------------------------------------ */

static int check_win(const int *selected) {
    if (!selected) return 0;
    
    for (int r = 0; r < GRID_DIM; r++) {
        int win = 1;
        for (int c = 0; c < GRID_DIM; c++) {
            if (!selected[r * GRID_DIM + c]) {
                win = 0;
                break;
            }
        }
        if (win) return 1;
    }
    
    for (int c = 0; c < GRID_DIM; c++) {
        int win = 1;
        for (int r = 0; r < GRID_DIM; r++) {
            if (!selected[r * GRID_DIM + c]) {
                win = 0;
                break;
            }
        }
        if (win) return 1;
    }
    
    if (selected[0] && selected[4] && selected[8]) return 1;
    if (selected[2] && selected[4] && selected[6]) return 1;
    
    return 0;
}

static void process_matches(game_data_t *data, process_state_t *proc,
                           const char picked[][MAX_WORD_LEN]) {
    if (!data || !proc || !picked) return;
    
    for (int p = 0; p < SEL_COLS; p++) {
        for (int g = 0; g < GRID_SIZE; g++) {
            if (strcmp(picked[p], data->grid[g]) == 0 && !data->selected[g]) {
                data->selected[g] = 1;
                if (proc->child_pids[g] > 0) {
                    kill(proc->child_pids[g], SIGTERM);
                }
                break;
            }
        }
    }
}

static void print_result(int win) {
    int th = get_terminal_height();
    int tw = get_terminal_width();
    
    if (tui_move(th, 1) < 0) return;
    
    const char *msg = win
        ? "*** BINGO!  You win!  A complete line cleared! ***"
        : "No winning line.  You lose.  Better luck next time!";
    
    int ml = (tw - (int)strlen(msg)) / 2;
    if (ml < 0) ml = 0;
    
    if (win) {
        printf("%*s" ANSI_GREEN ANSI_BOLD "%s" ANSI_RESET, ml, "", msg);
    } else {
        printf("%*s" ANSI_RED ANSI_BOLD "%s" ANSI_RESET, ml, "", msg);
    }
    
    printf("\033[?25h\n");
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* Message Processing                                                  */
/* ------------------------------------------------------------------ */

static void drain_child_messages(game_state_t *state) {
    if (!state || state->proc.pipe_fds[0] < 0) return;
    
    char msg[CHILD_MSG_LEN];
    ssize_t n = read(state->proc.pipe_fds[0], msg, CHILD_MSG_LEN);
    
    if (n == CHILD_MSG_LEN) {
        msg[CHILD_MSG_LEN - 1] = '\0';
        int ret = snprintf(state->current_insult, sizeof(state->current_insult), "%s", msg);
        if (ret < 0 || (size_t)ret >= sizeof(state->current_insult)) {
            state->current_insult[sizeof(state->current_insult) - 1] = '\0';
        }
    }
}

static void check_insult_timer(game_state_t *state) {
    if (!state) return;
    
    if (g_insult_tick) {
        g_insult_tick = 0;
        int ret = snprintf(state->current_insult, sizeof(state->current_insult), "%s",
                          insults[state->insult_idx % NUM_INSULTS]);
        if (ret < 0 || (size_t)ret >= sizeof(state->current_insult)) {
            state->current_insult[sizeof(state->current_insult) - 1] = '\0';
        }
        state->insult_idx++;
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

static int show_welcome_screen(void) {
    int ret = 0;
    ret += printf("\033[2J\033[H");
    ret += printf(ANSI_BOLD ANSI_CYAN
           "\n  Welcome to BINGO!\n"
           "  9 words have been secretly chosen and 9 child processes spawned.\n"
           "  Select 9 words from the list. Each match kills a child process.\n"
           "  Complete a row, column or diagonal to win!\n"
           "  The grid is hidden — it will be revealed at the end.\n" ANSI_RESET);
    
    if (ret < 0 || print_hidden_grid() < 0) return -1;
    
    ret += printf("  Press any key to start selection...\n");
    
    unsigned char tmp;
    if (read(STDIN_FILENO, &tmp, 1) != 1) {
        return -1;
    }
    
    return 0;
}

static int run_game_loop(game_state_t *state) {
    if (!state) return -1;
    
    memset(state->current_insult, 0, sizeof(state->current_insult));
    state->insult_idx = rand() % NUM_INSULTS;
    
    while (1) {
        drain_child_messages(state);
        check_insult_timer(state);
        
        if (print_selection_table(state) < 0) return -1;
        
        int k = read_key();
        handle_key_input(&state->sel, k);
        
        if (is_selection_complete(&state->sel)) break;
    }
    
    return 0;
}

static int initialize_game(game_state_t *state) {
    if (!state) return -1;
    
    memset(state, 0, sizeof(*state));
    init_process_state(&state->proc);
    
    g_cleanup_ctx.state = state;
    g_cleanup_ctx.initialized = 1;
    
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    
    /* Load words */
    load_result_t load_res = load_words(&state->data);
    if (load_res != LOAD_SUCCESS) {
        fprintf(stderr, "Failed to load words: error %d\n", load_res);
        return -1;
    }
    
    if (state->data.total_words < GRID_SIZE) {
        fprintf(stderr, "Need at least %d words in %s\n", GRID_SIZE, WORDS_FILE);
        return -1;
    }
    
    /* Fill grid */
    if (fill_grid(&state->data) < 0) {
        fprintf(stderr, "Failed to fill grid\n");
        return -1;
    }
    
    /* Create pipe */
    if (create_pipe(&state->proc) != PROC_SUCCESS) {
        fprintf(stderr, "Failed to create pipe\n");
        return -1;
    }
    
    /* Spawn children */
    if (spawn_children(&state->proc) != PROC_SUCCESS) {
        fprintf(stderr, "Failed to spawn children\n");
        return -1;
    }
    
    /* Setup parent pipe */
    if (setup_parent_pipe(&state->proc) != PROC_SUCCESS) {
        fprintf(stderr, "Failed to setup parent pipe\n");
        cleanup_children(&state->proc, GRID_SIZE);
        return -1;
    }
    
    /* Setup signal handler */
    if (setup_sigalrm() < 0) {
        fprintf(stderr, "Failed to setup signal handler\n");
        cleanup_children(&state->proc, GRID_SIZE);
        return -1;
    }
    
    /* Enable raw mode */
    if (enable_raw_mode(&state->ui) < 0) {
        fprintf(stderr, "Failed to enable raw mode\n");
        cleanup_children(&state->proc, GRID_SIZE);
        return -1;
    }
    
    if (atexit(atexit_cleanup) != 0) {
        fprintf(stderr, "Failed to register cleanup handler\n");
        restore_terminal(&state->ui);
        cleanup_children(&state->proc, GRID_SIZE);
        return -1;
    }
    
    /* Set alarm */
    alarm(INSULT_INTERVAL_SEC);
    
    /* Initialize selection state */
    init_selection_state(&state->sel, state->data.total_words);
    
    /* Shuffle word list */
    if (shuffle_word_list(&state->data) < 0) {
        fprintf(stderr, "Failed to shuffle word list\n");
        return -1;
    }
    
    return 0;
}

static void cleanup_game(game_state_t *state) {
    if (!state) return;
    
    alarm(0);
    
    if (state->proc.pipe_fds[0] >= 0) {
        close(state->proc.pipe_fds[0]);
        state->proc.pipe_fds[0] = -1;
    }
    
    kill_all_children(&state->proc);
    reap_all_children(&state->proc);
    
    restore_terminal(&state->ui);
}

static int finalize_game(game_state_t *state) {
    if (!state) return -1;
    
    /* Collect picks */
    char picked[SEL_COLS][MAX_WORD_LEN];
    collect_picks(&state->data, &state->sel, picked);
    
    /* Process matches */
    process_matches(&state->data, &state->proc, picked);
    
    /* Cleanup children */
    cleanup_game(state);
    
    /* Print result */
    if (print_final_result(picked, &state->data) < 0) return -1;
    
    int win = check_win(state->data.selected);
    print_result(win);
    
    return win ? 0 : 1;
}

int main(void) {
    game_state_t state;
    
    if (initialize_game(&state) < 0) {
        return 1;
    }
    
    if (show_welcome_screen() < 0) {
        cleanup_game(&state);
        return 1;
    }
    
    if (run_game_loop(&state) < 0) {
        cleanup_game(&state);
        return 1;
    }
    
    return finalize_game(&state);
}
