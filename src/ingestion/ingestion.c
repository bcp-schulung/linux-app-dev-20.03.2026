#include <asm-generic/errno.h>
#include <stdio.h>
#include <string.h>
#include <sqlite3.h>

int ReadCsvFile(const sqlite3 *db, const char* filename, char* delim) {
    
    FILE *fptr;
    sqlite3_stmt *stmt;

    // Open a file in read mode
    fptr = fopen(filename, "r");

    // Print some text if the file does not exist
    if(fptr == NULL) {
        printf("Not able to open the file '%s'", filename);
    } else {
        // Here we have taken size of array 1024 you can modify it
        char buffer[1024];

        int row = 0;
        int column = 0;

        while (fgets(buffer, 1024, fptr)) {

            if (row == 1)
                continue;

            // Splitting the data
            char* value = strtok(buffer, delim);

            // Insert values into sqlite
                    

        }

    }
    
    // Close the file
    fclose(fptr);

    return 0;
}


int main(void) {
    
    // Database object
    sqlite3 *db;
	
    // Open database
    const char *db_file_name = "weather.db";

    int rc = sqlite3_open_v2(db_file_name, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
		return rc;
	} else {
        printf("Successfully opend database\n");
    }

    // Read csv file and insert data to database
    int records = ReadCsvFile(db, "data/fake-station.csv", ",");

    // Close database
    sqlite3_close(db);

    printf("Closed database\n");

    return 0;
}

