#include <sqlite3.h>
#include "db.h"
#include "stdio.h"
#include <stdlib.h>
#include <string.h>

const char* getfield(char* line, int num)
{   
    printf("Try to get %d from %s", num,line);
    const char* tok;
    for (tok = strtok(line, ",");
            tok && *tok;
            tok = strtok(NULL, ",\n"))
    {
        if (!--num)
            return tok;
    }
    return NULL;
}

int main(void){
    if (remove("data.db") == 0) {
        printf("File deleted successfully.\n");
    } else {
        printf("Error: Unable to delete the file.\n");
    }
    sqlite3 * db;
    open_db(db);
    setup_db(db);

    FILE* stream = fopen("../../../data/fake-station.csv", "r");
    
    if (stream == NULL) {
        printf("File open failed!.\n");
        return 1;
    } else {
        printf("Opened csv.\n");
    }

    char line[1024];
    //read first line
    char * result = fgets(line, 1024, stream);

    printf("Result %s", result);
    while (fgets(line, 1024, stream))
    {   
        //printf("Read line %s.\n",line);
        char* tmp = strdup(line);
        printf("Field 3 would be %s\n", getfield(tmp, 3));
        // NOTE strtok clobbers tmp
        free(tmp);
    }


    write_record(db, "test", "stat1", 33.3);
    close_db(db);
    return 0;
}