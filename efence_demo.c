/*
 * efence_demo.c  —  standalone Electric Fence demonstration
 *
 * Reproduces the off-by-one heap overflow from bingo.c:shuffle_word_list().
 *
 * Build WITHOUT Electric Fence (bug goes undetected):
 *   gcc -g -o demo_noef efence_demo.c
 *   ./demo_noef
 *
 * Build WITH Electric Fence (SIGSEGV on first out-of-bounds write):
 *   gcc -g -o demo_ef efence_demo.c -lefence
 *   ./demo_ef
 *
 * Inspect with GDB:
 *   gdb ./demo_ef
 *   (gdb) run
 *   (gdb) backtrace
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_WORDS    512
#define MAX_WORD_LEN 256
#define WORD_COUNT   20

typedef struct {
    char all_words[MAX_WORDS][MAX_WORD_LEN];
    int  total_words;
} game_data_t;

static void shuffle(int *arr, int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

/* BUG: allocates (total_words - 1) ints, but the fill loop writes
 * total_words entries — one past the end of the allocation.
 * Electric Fence places a guard page immediately after the buffer,
 * so the first out-of-bounds write triggers a SIGSEGV. */
static void shuffle_word_list(game_data_t *data) {
    int *indices = malloc((data->total_words - 1) * sizeof(int));
    if (!indices) {
        perror("malloc");
        exit(1);
    }

    /* Off-by-one: writes indices[total_words - 1] past the allocation */
    for (int i = 0; i < data->total_words; i++) {
        indices[i] = i;
    }

    shuffle(indices, data->total_words);
    free(indices);
}

int main(void) {
    srand((unsigned)time(NULL));

    game_data_t data;
    data.total_words = WORD_COUNT;
    for (int i = 0; i < data.total_words; i++) {
        snprintf(data.all_words[i], MAX_WORD_LEN, "word%d", i);
    }

    printf("shuffle_word_list: allocating %d ints for %d words (off-by-one)...\n",
           data.total_words - 1, data.total_words);

    shuffle_word_list(&data);

    /* Without Electric Fence the overflow is silent and we reach here. */
    printf("Finished without crash — bug undetected (no Electric Fence).\n");
    return 0;
}
