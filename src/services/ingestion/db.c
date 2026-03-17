#include <sqlite3.h>
#include <stdio.h>

int open_db(sqlite3 *db){
    int rc = sqlite3_open("data.db", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    return  0;    
}
int close_db(sqlite3 *db){
    return sqlite3_close(db);    
}

int setup_db(sqlite3 *db){
    char *err_msg = 0;

    // Execute INSERT statement
    const char *sql = "CREATE TABLE Records (ID int PRIMARY KEY, Timestamp varchar(255), Station varchar(255),Temperature float);";

    int rc = sqlite3_exec(db, sql, 0, 0, &err_msg);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error setup: %s\n", err_msg);
        sqlite3_free(err_msg);
    } else {
        printf("Created table.\n");
    }
    return 0;
}

int write_record(sqlite3 *db, const char* timestamp, const char* station,float temperature){
    char *err_msg = 0;

    // Execute INSERT statement
    const char *sql = "INSERT INTO Record (Timestamp, Station, Temperature) VALUES ('1.1.26', 'Station1', 57,3);";
    int rc = sqlite3_exec(db, sql, 0, 0, &err_msg);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error record: %s\n", err_msg);
        sqlite3_free(err_msg);
    } else {
        printf("Data inserted successfully.\n");
    }
    return 0;
}
