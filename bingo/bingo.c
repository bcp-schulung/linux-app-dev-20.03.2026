#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <time.h>

#define NUM_ALL_WORDS 81
#define NUM_PICKED_WORDS 9
#define NUM_TRIES 9


int main(void) {

    FILE *file = fopen("words.txt", "r");
    if (file == NULL) {
        perror("Fehler beim Öffnen der Datei");
        return 1;
    }

    char line[256];
    int num_words = 0;
    
    char all_words[81][256];

    while (fgets(line, sizeof(line), file) != NULL) {
        for (int i = 0; i < sizeof(line); i++)
        {
            all_words[num_words][i] = line[i];
        }
        num_words++;
    }
     srand(time(NULL));
    // Choose random 9 words from 81
    int indices[9];
    for(int i = 0; i < 9; i++)
    {
        indices[i] = rand() % num_words;
    }


    // Create Child Processes
    static pid_t child_pids[9];

    for(int i = 0; i < 9; i++)
    {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork failed");
            exit(1);
        } else if (pid == 0) {
            // Child-Prozess greift auf den String zu
            // printf("Child received message: %s with index %d\n", all_words[indices[i]], indices[i]);
            exit(0);
        } else {
            // Parent-Prozess
            //printf("Parent process\n");
        }
        child_pids[i] = pid;
    }

    // User Auswahl. Hier: hard coded
    int picked_indices[28] = {0,1,2,3,4,5,6,7,8,9,10,11, 12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27};

    // Compare picked indices
    int selected[9];
    for (int p = 0; p < 28; p++)
    {
        for(int i = 0; i < 9; i++)
        {
            if ((indices[i] == picked_indices[p]) && !selected[i])
            {
                selected[i] = 1;
                //printf("Found index %d in picked_index %d\n", indices[i], picked_indices[p]);
                kill(child_pids[i], SIGTERM);
                break;
            }
        }
    }

    // Kill restliche Childs

    for(int i = 0; i < 9; i++)
    {
        if (!selected[i])
        {
            kill(child_pids[i], SIGTERM);
        }
    }

    int count_hits = 0;
    int positions[3];
    for(int i = 0; i < 9; i++)
    {
        if (selected[i])
        {
            
            printf("Found match in position %d\n", i);
            positions[count_hits] = i;
            count_hits++;
        }
    }
    if (count_hits >= 3)
    {
        for(int i = 0; i < count_hits-2; i++)
        {
            if( (positions[i] == 0 && positions[i+1]== 1 && positions[i+2] == 2) ||
                (positions[i] == 3 && positions[i+1]== 4 && positions[i+2] == 5) ||
                (positions[i] == 6 && positions[i+1]== 7 && positions[i+2] == 8) ||
                (positions[i] == 0 && positions[i+1]== 3 && positions[i+2] == 6) ||
                (positions[i] == 1 && positions[i+1]== 4 && positions[i+2] == 7) ||
                (positions[i] == 2 && positions[i+1]== 5 && positions[i+2] == 8) ||
                (positions[i] == 0 && positions[i+1]== 4 && positions[i+2] == 8) ||
                (positions[i] == 2 && positions[i+1]== 4 && positions[i+2] == 6))
            {
                printf("You win!\n");
            }
        }
    }
    

        

    

    fclose(file);
    return 0;
}