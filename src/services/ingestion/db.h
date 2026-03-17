#include <sqlite3.h>
int open_db(sqlite3 *db);
int close_db(sqlite3 *db);
int setup_db(sqlite3 *db);
int write_record(sqlite3 *db, const char* timestamp, const char* station,float temperature);