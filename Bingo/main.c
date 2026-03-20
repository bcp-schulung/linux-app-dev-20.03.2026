#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUF_ITEM_LEN 81
#define TXT_WIDTH 37

#define GRID_SIZE   9      /* 3x3 */
#define GRID_DIM    3

int readFile(const char *filename, char *buf_item) {
  FILE *fptr = fopen(filename, "r");
  int i = 0;

  if (fptr == NULL) {
    printf("Error: Could not open file.\n");
    return 1;
  }

  for (int i = 0; i < BUF_ITEM_LEN; i++) {
    fgets(&buf_item[i], TXT_WIDTH, fptr);
    // printf("%s", &buf_item[i]);
  }
  printf("\n");

  fclose(fptr);
  return 0;
}

// Fisher-Yates shuffle on an index array
static void shuffle(int *arr, int n) {
  for (int i = n - 1; i > 0; i--) {
    int j = rand() % (i + 1);
    int tmp = arr[i];
    arr[i] = arr[j];
    arr[j] = tmp;
  }
}

// Pick 9 unique random words for the grid
static void fill_grid(int *all_words[BUF_ITEM_LEN], int *grid) {
  int indices[BUF_ITEM_LEN];
  for (int i = 0; i < BUF_ITEM_LEN; i++)
    indices[i] = i;
  shuffle(indices, BUF_ITEM_LEN);
  for (int i = 0; i < GRID_SIZE; i++) {
    grid[i] = all_words[indices[i]];
    // strncpy(*grid[i], all_words[indices[i]], TXT_WIDTH - 1);
    // *grid[i][TXT_WIDTH - 1] = '\0';
  }
}

int main(void) {

  char buf_item[BUF_ITEM_LEN];
  int error = -2;
  error = readFile("words.txt", buf_item);
  printf("Check: %i\n", error);

  int grid[GRID_SIZE];

  fill_grid(buf_item, grid);

  return 0;
}