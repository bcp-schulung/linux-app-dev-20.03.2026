#include "ingestion.h"
#include "string.h"


int main(void)
{

    //Start ingestor, einlesen, in sqlite speichern und stoppen
    FILE* mfile = fopen("../../data/fake-station.csv", "r");

    if (!mfile)
    {
        printf("Error in reading file");
        return -1;
    }
    
    sqlite3 *db;
    int rc = sqlite3_open("../../data/weather.db", &db);
    if(rc != SQLITE_OK)
    {
        printf("Error in opening db");
    }


    int c = 0;
    char line[512];
    //char *token;
    while(fgets(line, sizeof(line), mfile))
    {
        for(char *tok = strtok(line, ","); tok != NULL; tok = strtok(NULL, ","))
        {
            printf("%s\n", tok);

        }
        // oder mit sscanf(line,....);
    }
    
    return 0;

}