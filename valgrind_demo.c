/*
 * valgrind_demo.c  –  Memory-error showcase based on bingo.c patterns
 *
 * Purpose
 * -------
 * Demonstrates four classes of bugs that Valgrind (Memcheck) catches,
 * using the same data structures and patterns as bingo.c:
 *
 *   1. HEAP BUFFER OVERFLOW   – writing past the end of malloc'd memory
 *   2. USE-AFTER-FREE         – reading freed memory (like a stale word pointer)
 *   3. MEMORY LEAK            – forgetting to free the shuffle-index array
 *   4. UNINITIALISED READ     – using grid data before it has been initialised
 *
 * Build
 * -----
 *   gcc -g -std=c11 -o valgrind_demo valgrind_demo.c
 *
 * Run without Valgrind  (may appear to work – undefined behaviour is sneaky)
 * -----
 *   ./valgrind_demo
 *
 * Run WITH Valgrind
 * -----------------
 *   valgrind --leak-check=full --track-origins=yes ./valgrind_demo
 *
 *   Key flags:
 *     --leak-check=full        report every individual leaked block
 *     --track-origins=yes      tell you WHERE uninitialised bytes came from
 *     --show-leak-kinds=all    include reachable + indirect leaks too
 *     --error-exitcode=1       useful in CI: non-zero exit on any error
 *
 * Expected Valgrind output (summary)
 * ------------------------------------
 *   ERROR SUMMARY: 4 errors from 4 contexts
 *   LEAK SUMMARY:
 *     definitely lost: N bytes in 1 blocks   (the index array)
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Constants mirrored from bingo.c                                    */
/* ------------------------------------------------------------------ */

#define GRID_SIZE    9
#define MAX_WORD_LEN 64

/* ------------------------------------------------------------------ */
/* Bug 1 – HEAP BUFFER OVERFLOW                                       */
/* ------------------------------------------------------------------ */
/*
 * bingo.c allocates `total_words * sizeof(int)` for the shuffle array
 * in fill_grid().  Here we allocate ONE ELEMENT TOO FEW and then write
 * past the end.
 *
 * Valgrind output you will see:
 *   ==PID== Invalid write of size 4
 *   ==PID==    at 0x...: demo_heap_overflow (valgrind_demo.c:NNN)
 *   ==PID==  Address 0x... is 0 bytes after a block of size N alloc'd
 */
static void demo_heap_overflow(void)
{
    puts("\n[Bug 1] Heap buffer overflow (off-by-one in shuffle array)");

    int n = GRID_SIZE;
    /* BUG: allocate n-1 ints, then write n ints */
    int *indices = malloc((n - 1) * sizeof(int));
    if (!indices) { perror("malloc"); return; }

    for (int i = 0; i < n; i++) {   /* <-- writes one element past the end */
        indices[i] = i;
    }

    printf("  indices[0]=%d  indices[%d]=%d\n", indices[0], n - 1, indices[n - 1]);
    free(indices);
}

/* ------------------------------------------------------------------ */
/* Bug 2 – USE-AFTER-FREE                                             */
/* ------------------------------------------------------------------ */
/*
 * Mimics a scenario where bingo.c frees a word buffer (e.g. after the
 * game ends) but then accidentally reads from it — like checking whether
 * a grid cell was selected after cleanup.
 *
 * Valgrind output you will see:
 *   ==PID== Invalid read of size 1
 *   ==PID==    at 0x...: demo_use_after_free (valgrind_demo.c:NNN)
 *   ==PID==  Address 0x... is N bytes inside a block of size M free'd
 */
static void demo_use_after_free(void)
{
    puts("\n[Bug 2] Use-after-free (stale word-buffer pointer)");

    char *word = malloc(MAX_WORD_LEN);
    if (!word) { perror("malloc"); return; }

    strncpy(word, "electric", MAX_WORD_LEN - 1);
    word[MAX_WORD_LEN - 1] = '\0';
    printf("  word before free: %s\n", word);

    free(word);                         /* word is now invalid */

    /* BUG: read from freed memory */
    printf("  word after free (stale): %s\n", word);
}

/* ------------------------------------------------------------------ */
/* Bug 3 – MEMORY LEAK                                                */
/* ------------------------------------------------------------------ */
/*
 * In bingo.c, fill_grid() correctly frees the index array.  Here we
 * forget to call free(), simulating what happens if an early-return
 * error path skips the cleanup.
 *
 * Valgrind output you will see:
 *   ==PID== LEAK SUMMARY:
 *   ==PID==    definitely lost: N bytes in 1 blocks
 *   ==PID==    at 0x...: demo_memory_leak (valgrind_demo.c:NNN)
 */
static void demo_memory_leak(void)
{
    puts("\n[Bug 3] Memory leak (shuffle-index array never freed)");

    int n = GRID_SIZE;
    int *indices = malloc(n * sizeof(int));
    if (!indices) { perror("malloc"); return; }

    for (int i = 0; i < n; i++) {
        indices[i] = i;
    }

    printf("  allocated %zu bytes for %d indices – never freed\n",
           n * sizeof(int), n);

    /* BUG: return without calling free(indices) */
}

/* ------------------------------------------------------------------ */
/* Bug 4 – UNINITIALISED MEMORY READ                                  */
/* ------------------------------------------------------------------ */
/*
 * Mirrors a situation where the grid array is passed to the renderer
 * before fill_grid() has actually populated it – for example if the
 * conditional that calls fill_grid() had a logic error.
 *
 * Valgrind output you will see (with --track-origins=yes):
 *   ==PID== Conditional jump or move depends on uninitialised value(s)
 *   ==PID==    at 0x...: demo_uninit_read (valgrind_demo.c:NNN)
 *   ==PID==  Uninitialised value was created by a heap allocation
 *   ==PID==    at 0x...: demo_uninit_read (valgrind_demo.c:NNN)
 */
static void demo_uninit_read(void)
{
    puts("\n[Bug 4] Uninitialised memory read (grid rendered before fill)");

    /* Allocate grid but intentionally skip memset / strcpy */
    char (*grid)[MAX_WORD_LEN] = malloc(GRID_SIZE * MAX_WORD_LEN);
    if (!grid) { perror("malloc"); return; }

    /* BUG: read uninitialised bytes via strlen() / printf() */
    for (int i = 0; i < GRID_SIZE; i++) {
        /* This branch depends on uninitialised bytes → Valgrind fires */
        if (grid[i][0] != '\0') {
            printf("  grid[%d] = %s\n", i, grid[i]);
        }
    }

    free(grid);
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    puts("=== valgrind_demo: bingo.c memory-error showcase ===");
    puts("Run under Valgrind to see all four bugs reported:\n");
    puts("  valgrind --leak-check=full --track-origins=yes ./valgrind_demo");

    demo_heap_overflow();
    demo_use_after_free();
    demo_memory_leak();
    demo_uninit_read();

    puts("\n=== done ===");
    return 0;
}
