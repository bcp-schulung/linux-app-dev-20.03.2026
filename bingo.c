/*
 * bingo.c - A terminal bingo game using words from words.txt
 *
 * Compile: gcc -o bingo bingo.c
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
#include <pthread.h>
#include <errno.h>

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

#define GRID_SIZE   9      /* 3x3 */
#define GRID_DIM    3
#define MAX_WORDS   512
#define MAX_WORD_LEN 128
#define WORDS_FILE  "words.txt"
#define INSULT_INTERVAL_SEC 60

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

/* ------------------------------------------------------------------ */
/* Child process insults (written to parent via pipe every few secs)  */
/* ------------------------------------------------------------------ */

#define CHILD_MSG_LEN 128

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
/* Globals                                                             */
/* ------------------------------------------------------------------ */

static char  all_words[MAX_WORDS][MAX_WORD_LEN];
static int   total_words = 0;

static char  grid[GRID_SIZE][MAX_WORD_LEN]; /* the 3x3 board words      */
static pid_t child_pids[GRID_SIZE];          /* one child per grid cell  */
static int   selected[GRID_SIZE];            /* 1 = user has picked this */

static struct termios orig_termios;
static volatile sig_atomic_t insult_tick = 0; /* set by SIGALRM           */
static int insult_pipe[2] = {-1, -1};          /* children -> parent msgs  */

/* ------------------------------------------------------------------ */
/* Terminal helpers                                                    */
/* ------------------------------------------------------------------ */

static void restore_terminal(void) {
    /* Show cursor again before restoring terminal */
    printf("\033[?25h");
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

static void enable_raw_mode(void) {
    struct termios raw;
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(restore_terminal);
    raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

/* ------------------------------------------------------------------ */
/* SIGALRM handler – triggers insult                                  */
/* ------------------------------------------------------------------ */

static void sigalrm_handler(int sig) {
    (void)sig;
    insult_tick++;
    alarm(INSULT_INTERVAL_SEC);
}

/* ------------------------------------------------------------------ */
/* Child process behaviour                                             */
/* ------------------------------------------------------------------ */

static void child_main(int index) {
    /* Close the read end – we only write */
    close(insult_pipe[0]);
    /* Each child gets its own random seed */
    srand((unsigned)time(NULL) ^ (unsigned)getpid() ^ (unsigned)index);
    while (1) {
        /* Wait 3-6 seconds between insults */
        sleep(3 + rand() % 4);
        const char *ins = child_insults[rand() % NUM_CHILD_INSULTS];
        char msg[CHILD_MSG_LEN];
        /* Include the child index so messages feel distinct */
        snprintf(msg, sizeof(msg), "[Process %d] %s", index + 1, ins);
        /* Pad to fixed length for atomic pipe writes */
        size_t len = strlen(msg);
        if (len < CHILD_MSG_LEN - 1)
            memset(msg + len, 0, CHILD_MSG_LEN - len);
        /* write() is async-signal-safe; ignore errors (parent may have closed) */
        if (write(insult_pipe[1], msg, CHILD_MSG_LEN) < 0) break;
    }
    exit(0);
}

/* ------------------------------------------------------------------ */
/* Load words from file                                                */
/* ------------------------------------------------------------------ */

static int load_words(void) {
    FILE *f = fopen(WORDS_FILE, "r");
    if (!f) {
        perror("fopen: " WORDS_FILE);
        return -1;
    }
    char line[MAX_WORD_LEN];
    while (fgets(line, sizeof(line), f) && total_words < MAX_WORDS) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[--len] = '\0';
        if (len == 0) continue;
        strncpy(all_words[total_words], line, MAX_WORD_LEN - 1);
        all_words[total_words][MAX_WORD_LEN - 1] = '\0';
        total_words++;
    }
    fclose(f);
    return total_words;
}

/* ------------------------------------------------------------------ */
/* Fisher-Yates shuffle on an index array                             */
/* ------------------------------------------------------------------ */

static void shuffle(int *arr, int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
    }
}

/* ------------------------------------------------------------------ */
/* Pick 9 unique random words for the grid                            */
/* ------------------------------------------------------------------ */

static void fill_grid(void) {
    int indices[MAX_WORDS];
    for (int i = 0; i < total_words; i++) indices[i] = i;
    shuffle(indices, total_words);
    for (int i = 0; i < GRID_SIZE; i++) {
        strncpy(grid[i], all_words[indices[i]], MAX_WORD_LEN - 1);
        grid[i][MAX_WORD_LEN - 1] = '\0';
    }
}

/* ------------------------------------------------------------------ */
/* Display helpers                                                     */
/* ------------------------------------------------------------------ */

/* ANSI codes */
#define ANSI_RESET   "\033[0m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_GREEN   "\033[1;32m"
#define ANSI_CYAN    "\033[1;36m"
#define ANSI_RED     "\033[1;31m"
#define ANSI_YELLOW  "\033[1;33m"
#define ANSI_BG_BLUE "\033[44m"

/* ------------------------------------------------------------------ */
/* Terminal width & dynamic cell sizing                                */
/* ------------------------------------------------------------------ */

static int get_terminal_width(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return (int)ws.ws_col;
    return 80;
}

static int get_terminal_height(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
        return (int)ws.ws_row;
    return 24;
}

/* Move cursor to absolute 1-based row/col */
static void tui_move(int row, int col) {
    printf("\033[%d;%dH", row, col);
}

/* Full TUI clear: erase screen, hide cursor, home */
static void tui_clear(void) {
    printf("\033[2J\033[H\033[?25l");
}

/* Cell width for dual side-by-side display.
 * Layout: "  "(2) + grid(3*cw+4) + "     "(5) + grid(3*cw+4) = 6*cw+15 */
static int cell_width_dual(void) {
    int cw = (get_terminal_width() - 15) / 6;
    if (cw < 8)  cw = 8;
    if (cw > 40) cw = 40;
    return cw;
}

/* Cell width for a single centred grid.
 * Layout: "  "(2) + grid(3*cw+4)  →  fill terminal width.          */
static int cell_width_single(void) {
    int cw = (get_terminal_width() - 6) / 3;
    if (cw < 8)  cw = 8;
    if (cw > 60) cw = 60;
    return cw;
}

/* Inner height in lines giving visually square cells.
 * Monospace fonts are ~2× taller than wide, so height = cw/4.      */
static int cell_inner_height(int cw) {
    int h = cw / 4;
    if (h < 1) h = 1;
    if (h > 4) h = 4;
    return h;
}

/* Print one horizontal rule for GRID_DIM columns of width cw */
static void print_hrule(int cw) {
    printf("+");
    for (int c = 0; c < GRID_DIM; c++) {
        for (int i = 0; i < cw; i++) putchar('-');
        putchar('+');
    }
}

/* Print `word` centred in `cw` chars; green-bold when highlight=1 */
static void print_cell_word(const char *word, int cw, int highlight) {
    char disp[MAX_WORD_LEN];
    strncpy(disp, word, sizeof(disp) - 1);
    disp[sizeof(disp) - 1] = '\0';
    if ((int)strlen(disp) > cw) {
        if (cw >= 4) {
            disp[cw - 3] = '.';
            disp[cw - 2] = '.';
            disp[cw - 1] = '.';
        }
        disp[cw] = '\0';
    }
    int wlen = (int)strlen(disp);
    int lp   = (cw - wlen) / 2;
    int rp   = (cw - wlen) - lp;
    if (lp < 0) lp = 0;
    if (rp < 0) rp = 0;
    if (highlight) printf(ANSI_GREEN ANSI_BOLD);
    printf("%*s%s%*s", lp, "", disp, rp, "");
    if (highlight) printf(ANSI_RESET);
}

/* Hidden 3x3 grid shown at game start — fills the terminal */
static void print_hidden_grid(void) {
    int tw        = get_terminal_width();
    int th        = get_terminal_height();
    int cw        = cell_width_single();
    int inner_h   = cell_inner_height(cw);
    int word_line = inner_h / 2;
    int gw        = GRID_DIM * cw + GRID_DIM + 1;
    int margin    = (tw - gw) / 2;
    if (margin < 0) margin = 0;

    /* Total lines used: 1 title + 1 blank + GRID_DIM*(inner_h+1) + 1 last rule + 2 footer */
    int grid_lines = GRID_DIM * (inner_h + 1) + 1;
    int used       = 1 + 1 + grid_lines + 2;
    int top_pad    = (th - used) / 2;
    if (top_pad < 0) top_pad = 0;

    tui_clear();
    tui_move(top_pad + 1, 1);

    /* Title centred */
    const char *title = "=== BINGO ===";
    int tl = (tw - (int)strlen(title)) / 2;
    if (tl < 0) tl = 0;
    printf("%*s" ANSI_BOLD ANSI_CYAN "%s" ANSI_RESET "\n\n", tl, "", title);

    for (int r = 0; r < GRID_DIM; r++) {
        printf("%*s", margin, ""); print_hrule(cw); printf("\n");
        for (int l = 0; l < inner_h; l++) {
            printf("%*s|", margin, "");
            for (int c = 0; c < GRID_DIM; c++) {
                if (l == word_line)
                    print_cell_word("[ ? ]", cw, 0);
                else
                    printf("%*s", cw, "");
                putchar('|');
            }
            printf("\n");
        }
    }
    printf("%*s", margin, ""); print_hrule(cw); printf("\n");

    /* Footer */
    const char *sub = "9 words hidden  \xe2\x80\x94  revealed at the end";
    int sl = (tw - (int)strlen(sub)) / 2;
    if (sl < 0) sl = 0;
    printf("\n%*s" ANSI_CYAN "%s" ANSI_RESET "\n", sl, "", sub);

    /* Prompt pinned to bottom */
    const char *prompt = "Press any key to start selection...";
    int pl = (tw - (int)strlen(prompt)) / 2;
    if (pl < 0) pl = 0;
    tui_move(th, 1);
    printf("%*s" ANSI_YELLOW "%s" ANSI_RESET, pl, "", prompt);
    fflush(stdout);
}

/* Final reveal: YOUR PICKS and BINGO GRID side by side — fills terminal */
static void print_final_result(char picked[][MAX_WORD_LEN]) {
    int tw        = get_terminal_width();
    int th        = get_terminal_height();
    int cw        = cell_width_dual();
    int inner_h   = cell_inner_height(cw);
    int word_line = inner_h / 2;
    int gw        = GRID_DIM * cw + GRID_DIM + 1;
    int gap       = tw - 2 * gw;   /* space between the two grids */
    if (gap < 3) gap = 3;
    int left_off  = (tw - (2 * gw + gap)) / 2;
    if (left_off < 0) left_off = 0;

    /* Mark which of the 9 picks matched any grid word */
    int pick_matched[9] = {0};
    for (int p = 0; p < 9; p++)
        for (int g = 0; g < GRID_SIZE; g++)
            if (strcmp(picked[p], grid[g]) == 0) { pick_matched[p] = 1; break; }

    /* Total lines: 1 title + 1 blank + 1 header + 1 blank
     *              + GRID_DIM*(inner_h+1) + 1 last rule           */
    int grid_lines = GRID_DIM * (inner_h + 1) + 1;
    int used       = 1 + 1 + 1 + 1 + grid_lines;
    int top_pad    = (th - used) / 2;
    if (top_pad < 1) top_pad = 1;

    tui_clear();
    tui_move(top_pad, 1);

    /* Title */
    const char *title = "=== FINAL RESULT ===";
    int tl = (tw - (int)strlen(title)) / 2;
    if (tl < 0) tl = 0;
    printf("%*s" ANSI_BOLD "%s" ANSI_RESET "\n\n", tl, "", title);

    /* Column headers */
    const char *lhdr = "YOUR PICKS";
    const char *rhdr = "BINGO GRID";
    int ll = (gw - (int)strlen(lhdr)) / 2;  if (ll < 0) ll = 0;
    int rl = (gw - (int)strlen(rhdr)) / 2;  if (rl < 0) rl = 0;
    printf("%*s%*s" ANSI_BOLD ANSI_CYAN "%s" ANSI_RESET "%*s",
           left_off, "", ll, "", lhdr, gw - ll - (int)strlen(lhdr), "");
    printf("%*s", gap, "");
    printf("%*s" ANSI_BOLD ANSI_CYAN "%s" ANSI_RESET "\n\n",
           rl, "", rhdr);

    for (int r = 0; r < GRID_DIM; r++) {
        /* top border */
        printf("%*s", left_off, ""); print_hrule(cw);
        printf("%*s", gap, "");     print_hrule(cw); printf("\n");

        for (int l = 0; l < inner_h; l++) {
            printf("%*s|", left_off, "");
            for (int c = 0; c < GRID_DIM; c++) {
                int pi = r * GRID_DIM + c;
                if (l == word_line)
                    print_cell_word(picked[pi], cw, pick_matched[pi]);
                else
                    printf("%*s", cw, "");
                putchar('|');
            }
            printf("%*s|", gap, "");
            for (int c = 0; c < GRID_DIM; c++) {
                int gi = r * GRID_DIM + c;
                if (l == word_line)
                    print_cell_word(grid[gi], cw, selected[gi]);
                else
                    printf("%*s", cw, "");
                putchar('|');
            }
            printf("\n");
        }
    }
    /* bottom border */
    printf("%*s", left_off, ""); print_hrule(cw);
    printf("%*s", gap, "");     print_hrule(cw); printf("\n");
}

/* ------------------------------------------------------------------ */
/* Build the word list table for selection                             */
/* Uses full terminal width. When the terminal is too narrow to show  */
/* all 9 columns at MIN_COL_W chars each, falls back to 3-column      */
/* pages: cols 1-3, 4-6, 7-9.                                         */
/* ------------------------------------------------------------------ */

#define SEL_COLS   9
#define MIN_COL_W  10   /* minimum useful column width                 */
#define PAGE_SIZE  3    /* columns per page in narrow mode             */

static void print_selection_table(int cur_col, int cur_row,
                                  int col_confirmed[SEL_COLS],
                                  int col_choice[SEL_COLS],
                                  int col_heights[SEL_COLS],
                                  char words[][MAX_WORD_LEN],
                                  int nwords __attribute__((unused)),
                                  const char *insult_msg) {

    int tw = get_terminal_width();
    int th = get_terminal_height();

    /* Decide layout ------------------------------------------------- */
    int full_col_w = (tw - 2 - (SEL_COLS - 1)) / SEL_COLS;
    int use_pages  = (full_col_w < MIN_COL_W);

    int col_start, col_end, col_w;
    if (use_pages) {
        int page  = cur_col / PAGE_SIZE;
        col_start = page * PAGE_SIZE;
        col_end   = col_start + PAGE_SIZE - 1;
        if (col_end >= SEL_COLS) col_end = SEL_COLS - 1;
        int n = col_end - col_start + 1;
        col_w = (tw - 2 - (n - 1)) / n;
    } else {
        col_start = 0;
        col_end   = SEL_COLS - 1;
        col_w     = full_col_w;
    }
    if (col_w < 4) col_w = 4;

    int vis_cols  = col_end - col_start + 1;
    int max_rows  = 0;
    for (int c = col_start; c <= col_end; c++)
        if (col_heights[c] > max_rows) max_rows = col_heights[c];

    /* Count lines this render will use:
     *  1 header-line + 1 blank + 1 col-hdr + 1 sep + max_rows + 1 status
     *  + (insult ? 1 : 0)  pinned to bottom row (not counted here)      */
    int content_lines = 1 + 1 + 1 + 1 + max_rows + 1;
    int top_pad = (th - 1 - content_lines) / 2;  /* reserve 1 row for footer */
    if (top_pad < 0) top_pad = 0;

    tui_clear();
    tui_move(top_pad + 1, 1);

    /* Header -------------------------------------------------------- */
    if (use_pages) {
        int page = cur_col / PAGE_SIZE;
        int hl = (tw - 60) / 2; if (hl < 0) hl = 0;
        printf("%*s" ANSI_BOLD "Select one word per column  "
               ANSI_CYAN "(wasd/arrows | ENTER=confirm)  "
               ANSI_YELLOW "[Page %d/3: cols %d-%d]"
               ANSI_RESET "\n\n",
               hl, "", page + 1, col_start + 1, col_end + 1);
    } else {
        int hl = (tw - 56) / 2; if (hl < 0) hl = 0;
        printf("%*s" ANSI_BOLD "Select one word per column  "
               ANSI_CYAN "(wasd / arrows  |  ENTER=confirm column)"
               ANSI_RESET "\n\n", hl, "");
    }

    /* Left margin to centre the table */
    int table_w = vis_cols * col_w + (vis_cols - 1);
    int lm = (tw - table_w) / 2;
    if (lm < 0) lm = 0;

    /* Column headers */
    printf("%*s", lm, "");
    for (int c = col_start; c <= col_end; c++) {
        char hdr[16];
        snprintf(hdr, sizeof(hdr), "Col %d", c + 1);
        int space = col_w - (int)strlen(hdr);
        if (space < 0) space = 0;
        printf(ANSI_BOLD "%s%*s" ANSI_RESET, hdr, space, "");
        if (c < col_end) printf(" ");
    }
    printf("\n");

    /* Separator */
    printf("%*s", lm, "");
    for (int c = col_start; c <= col_end; c++) {
        for (int i = 0; i < col_w; i++) printf("-");
        if (c < col_end) printf(" ");
    }
    printf("\n");

    /* Rows ---------------------------------------------------------- */
    for (int r = 0; r < max_rows; r++) {
        printf("%*s", lm, "");
        for (int c = col_start; c <= col_end; c++) {
            int base = 0;
            for (int x = 0; x < c; x++) base += col_heights[x];
            int wi = base + r;

            if (r >= col_heights[c]) {
                printf("%-*s", col_w, "");
                if (c < col_end) printf(" ");
                continue;
            }

            int is_cursor    = (c == cur_col && r == cur_row);
            int is_confirmed = (col_confirmed[c] && col_choice[c] == r);

            if (is_cursor)         printf(ANSI_BG_BLUE ANSI_BOLD);
            else if (is_confirmed) printf(ANSI_GREEN ANSI_BOLD);

            char display[MAX_WORD_LEN];
            strncpy(display, words[wi], sizeof(display) - 1);
            display[sizeof(display) - 1] = '\0';
            if ((int)strlen(display) > col_w) {
                if (col_w >= 4) {
                    display[col_w - 3] = '.';
                    display[col_w - 2] = '.';
                    display[col_w - 1] = '.';
                }
                display[col_w] = '\0';
            }

            printf("%-*s", col_w, display);

            if (is_cursor || is_confirmed) printf(ANSI_RESET);
            if (c < col_end) printf(" ");
        }
        printf("\n");
    }

    /* Status bar ---------------------------------------------------- */
    int done = 0;
    for (int c = 0; c < SEL_COLS; c++) if (col_confirmed[c]) done++;
    {
        char status[256];
        int n = snprintf(status, sizeof(status), "Confirmed: %d / %d", done, SEL_COLS);
        int sl = (tw - n) / 2; if (sl < 0) sl = 0;
        printf("\n%*s" ANSI_YELLOW "%s" ANSI_RESET "\n", sl, "", status);
    }

    /* Insult / hint pinned to the last terminal row */
    tui_move(th, 1);
    if (insult_msg && insult_msg[0]) {
        int il = (tw - (int)strlen(insult_msg) - 4) / 2;
        if (il < 0) il = 0;
        printf("%*s" ANSI_RED ANSI_BOLD ">> %s <<" ANSI_RESET, il, "", insult_msg);
    } else {
        const char *hint = use_pages
            ? "A/D: switch page  W/S: pick word  ENTER: confirm"
            : "A/D: change column  W/S: pick word  ENTER: confirm";
        int hl = (tw - (int)strlen(hint)) / 2; if (hl < 0) hl = 0;
        printf("%*s" ANSI_CYAN "%s" ANSI_RESET, hl, "", hint);
    }
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* Read a key from raw terminal; returns a simple code.               */
/* ------------------------------------------------------------------ */

#define KEY_UP    1
#define KEY_DOWN  2
#define KEY_LEFT  3
#define KEY_RIGHT 4
#define KEY_ENTER 5
#define KEY_OTHER 0

static int read_key(void) {
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return KEY_OTHER;

    if (c == '\r' || c == '\n') return KEY_ENTER;

    /* Arrow keys come as ESC [ A/B/C/D */
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

    /* WASD */
    switch (c) {
        case 'w': case 'W': return KEY_UP;
        case 's': case 'S': return KEY_DOWN;
        case 'a': case 'A': return KEY_LEFT;
        case 'd': case 'D': return KEY_RIGHT;
    }

    return KEY_OTHER;
}

/* ------------------------------------------------------------------ */
/* Bingo win check                                                     */
/* ------------------------------------------------------------------ */

static int check_win(void) {
    /* rows */
    for (int r = 0; r < GRID_DIM; r++) {
        int win = 1;
        for (int c = 0; c < GRID_DIM; c++)
            if (!selected[r * GRID_DIM + c]) { win = 0; break; }
        if (win) return 1;
    }
    /* columns */
    for (int c = 0; c < GRID_DIM; c++) {
        int win = 1;
        for (int r = 0; r < GRID_DIM; r++)
            if (!selected[r * GRID_DIM + c]) { win = 0; break; }
        if (win) return 1;
    }
    /* diagonals */
    if (selected[0] && selected[4] && selected[8]) return 1;
    if (selected[2] && selected[4] && selected[6]) return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(void) {
    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    /* ---- Load words ---- */
    if (load_words() < GRID_SIZE) {
        fprintf(stderr, "Need at least %d words in %s\n", GRID_SIZE, WORDS_FILE);
        return 1;
    }

    /* ---- Fill the 3x3 grid ---- */
    fill_grid();
    memset(selected, 0, sizeof(selected));

    /* ---- Create pipe for child insult messages ---- */
    if (pipe(insult_pipe) < 0) {
        perror("pipe");
        return 1;
    }

    /* ---- Spawn one child per grid cell ---- */
    for (int i = 0; i < GRID_SIZE; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            /* Kill any already-spawned children before exiting */
            for (int j = 0; j < i; j++) kill(child_pids[j], SIGKILL);
            return 1;
        }
        if (pid == 0) {
            child_main(i);
            /* Not reached */
            exit(0);
        }
        child_pids[i] = pid;
    }

    /* ---- Parent: close write end, set read end non-blocking ---- */
    close(insult_pipe[1]);
    insult_pipe[1] = -1;
    fcntl(insult_pipe[0], F_SETFL, O_NONBLOCK);

    /* ---- Set up SIGALRM for insults ---- */
    struct sigaction sa_alrm;
    memset(&sa_alrm, 0, sizeof(sa_alrm));
    sa_alrm.sa_handler = sigalrm_handler;
    sigemptyset(&sa_alrm.sa_mask);
    sa_alrm.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa_alrm, NULL);
    alarm(INSULT_INTERVAL_SEC);

    /* ---- Enable raw mode ---- */
    enable_raw_mode();

    /* ---- Print initial grid ---- */
    printf("\033[2J\033[H"); /* clear screen */
    printf(ANSI_BOLD ANSI_CYAN
           "\n  Welcome to BINGO!\n"
           "  9 words have been secretly chosen and 9 child processes spawned.\n"
           "  Select 9 words from the list. Each match kills a child process.\n"
           "  Complete a row, column or diagonal to win!\n"
           "  The grid is hidden — it will be revealed at the end.\n" ANSI_RESET);

    print_hidden_grid();
    printf("  Press any key to start selection...\n");
    {
        unsigned char tmp;
        read(STDIN_FILENO, &tmp, 1);
    }

    /* ---- Build selection table distribution ---- */
    /* Distribute all_words evenly across SEL_COLS columns */
    int col_heights[SEL_COLS] = {0};
    {
        int base = total_words / SEL_COLS;
        int rem  = total_words % SEL_COLS;
        for (int c = 0; c < SEL_COLS; c++)
            col_heights[c] = base + (c < rem ? 1 : 0);
    }

    /* Shuffle the global word list so selection order is random */
    {
        int indices[MAX_WORDS];
        for (int i = 0; i < total_words; i++) indices[i] = i;
        shuffle(indices, total_words);
        char tmp_words[MAX_WORDS][MAX_WORD_LEN];
        for (int i = 0; i < total_words; i++)
            strncpy(tmp_words[i], all_words[indices[i]], MAX_WORD_LEN - 1);
        memcpy(all_words, tmp_words, sizeof(tmp_words));
    }

    int col_confirmed[SEL_COLS] = {0};  /* has the user confirmed a pick? */
    int col_choice[SEL_COLS]    = {0};  /* which row was confirmed         */
    int cur_col = 0, cur_row = 0;

    int insult_idx = rand() % NUM_INSULTS;
    char current_insult[256] = {0};  /* empty = show hint */

    /* ---- Selection loop ---- */
    while (1) {
        /* Drain one message from child insult pipe (non-blocking) */
        {
            char msg[CHILD_MSG_LEN];
            ssize_t n = read(insult_pipe[0], msg, CHILD_MSG_LEN);
            if (n == CHILD_MSG_LEN) {
                msg[CHILD_MSG_LEN - 1] = '\0';
                snprintf(current_insult, sizeof(current_insult), "%s", msg);
            }
        }

        /* Check for pending SIGALRM insult (overrides child message) */
        if (insult_tick > 0) {
            insult_tick = 0;
            snprintf(current_insult, sizeof(current_insult), "%s",
                     insults[insult_idx % NUM_INSULTS]);
            insult_idx++;
        }

        /* Redraw */
        print_selection_table(cur_col, cur_row,
                              col_confirmed, col_choice,
                              col_heights, all_words, total_words,
                              current_insult);

        int k = read_key();

        if (k == KEY_LEFT) {
            cur_col = (cur_col - 1 + SEL_COLS) % SEL_COLS;
            cur_row = 0;
        } else if (k == KEY_RIGHT) {
            cur_col = (cur_col + 1) % SEL_COLS;
            cur_row = 0;
        } else if (k == KEY_UP) {
            if (cur_row > 0) cur_row--;
        } else if (k == KEY_DOWN) {
            if (cur_row < col_heights[cur_col] - 1) cur_row++;
        } else if (k == KEY_ENTER) {
            col_confirmed[cur_col] = 1;
            col_choice[cur_col]    = cur_row;
        }

        /* Check if all 9 columns confirmed */
        int done = 0;
        for (int c = 0; c < SEL_COLS; c++) if (col_confirmed[c]) done++;
        if (done == SEL_COLS) break;
    }

    /* ---- Cancel alarm & close pipe ---- */
    alarm(0);
    close(insult_pipe[0]);
    insult_pipe[0] = -1;

    /* ---- Collect the 9 selected words ---- */
    char picked[SEL_COLS][MAX_WORD_LEN];
    {
        int base = 0;
        for (int c = 0; c < SEL_COLS; c++) {
            int wi = base + col_choice[c];
            strncpy(picked[c], all_words[wi], MAX_WORD_LEN - 1);
            picked[c][MAX_WORD_LEN - 1] = '\0';
            base += col_heights[c];
        }
    }

    /* ---- Match picked words to grid, kill corresponding children ---- */
    for (int p = 0; p < SEL_COLS; p++) {
        for (int g = 0; g < GRID_SIZE; g++) {
            if (strcmp(picked[p], grid[g]) == 0 && !selected[g]) {
                selected[g] = 1;
                kill(child_pids[g], SIGTERM);
                break;
            }
        }
    }

    /* ---- Kill any children that were not matched, then reap all ---- */
    for (int i = 0; i < GRID_SIZE; i++) {
        if (!selected[i])
            kill(child_pids[i], SIGTERM);
    }
    for (int i = 0; i < GRID_SIZE; i++) {
        waitpid(child_pids[i], NULL, 0);
    }

    /* ---- Print final result ---- */
    print_final_result(picked);

    /* ---- Announce result (pinned to bottom row) ---- */
    int win = check_win();
    {
        int th = get_terminal_height();
        int tw = get_terminal_width();
        tui_move(th, 1);
        const char *msg = win
            ? "*** BINGO!  You win!  A complete line cleared! ***"
            : "No winning line.  You lose.  Better luck next time!";
        int ml = (tw - (int)strlen(msg)) / 2; if (ml < 0) ml = 0;
        if (win)
            printf("%*s" ANSI_GREEN ANSI_BOLD "%s" ANSI_RESET, ml, "", msg);
        else
            printf("%*s" ANSI_RED ANSI_BOLD "%s" ANSI_RESET, ml, "", msg);
        /* Show cursor again and move to a clean line */
        printf("\033[?25h\n");
        fflush(stdout);
    }

    return win ? 0 : 1;
}
