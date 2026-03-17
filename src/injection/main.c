#include <stdio.h>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
  
static inline int addToDB(char* lineBuffer) {
    char *lineTokens[128];
    memset(lineTokens, 0 , sizeof(lineTokens));

    int idxToken = 0;  
    char* token = NULL;  
    token = strtok(lineBuffer, ",");
    while (token != NULL) {
        printf (" %s ", token);
        lineTokens[idxToken] = token;
        token = strtok(NULL, ",");


    } 


    printf("\n");
    return 0;
}

int main(int argc, char *argv[]) {

    char *infile;
    char c;
    while ((c = getopt (argc, argv, "i:o")) != -1) {
        switch (c) {
            case 'i':
                printf("Input i has option %s\n", optarg);
                infile = optarg;
                break;
            case 'g':
                printf("Option g has option %s\n", optarg);
                break;
            case 'r':
                printf("Option r has option %s\n", optarg);
                break;
            case 't':
                printf("Option t has option %s\n", optarg);
                break;
        }
    }

    printf("%s %s\n", "Infile", infile);
    FILE* fd = fopen(infile, "r");
    char lineBuffer[4096];
   
   
    int idx = 0;
   
    while (fgets(lineBuffer, sizeof(lineBuffer), fd) != 0) {
        printf("\r Format line %i : ", idx);
        addToDB(lineBuffer);
       
         ++idx;
    }

        

       
    

    fclose(fd);
    printf("%s\n", "Hello World");
    return 0;
}