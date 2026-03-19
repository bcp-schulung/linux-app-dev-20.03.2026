#include <stdio.h>

// #include "aggregation/aggregation.h"
// #include "cli/cli.h"
// #include "discovery/discovery.h"
#include "injection/injection.h"
// #include "query/query.h"

#include <sqlite3.h>

int main(void) {
  printf("Hello World!\n");

  sqlite3 *db;  // database for sqlite
  int test = readDatabase(db);
  

  return 0;
}