//gcc ingestion.c -o ingestion -lsqlite3; ./ingestion

#include <stdio.h>
#include <sqlite3.h>
#include <stdlib.h>

//config later
const char* input = "/home/devuser/linux-app-dev-20.03.2026/data/fake-station2.csv";
const char* sqliteDB = "/home/devuser/linux-app-dev-20.03.2026/data/weather.db";

int main(void) {

    FILE* f = fopen(input, "r");
    
    sqlite3** db;

    sqlite3_open(sqliteDB, db);

    char* line;
    size_t length;
    int ret;

    while (getline(&line, &length, f) > 0) {
        printf("%s", line);
    }
    free(line);

    return 0;
}
