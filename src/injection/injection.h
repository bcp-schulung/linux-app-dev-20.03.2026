#include <sqlite3.h> 
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/** @brief Read database */
int readDatabase(sqlite3 *db);
int import_csv_to_sqlite(sqlite3 *db, const char *filename, const char *table_name);