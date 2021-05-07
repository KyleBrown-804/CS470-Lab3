#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include<stdatomic.h>

#define ERROR_MESSAGE "The program exited with code 1\n\n"

atomic_int endGame = 0;
pthread_mutex_t mLock;

// Process memory to share with the current process's multiple threads
struct processMemory {
    FILE * mFile;
    int numSpaces;
    int rows;
    int cols;
};

// check for valid logical inputs in command line args
int *checkCommand(int numArgs, char *args[]) {
    for (int i = 1; i < numArgs; i++) {
        if (!isdigit(*args[i])) {
            printf("Argument #%d was not a valid integer\n%s", i + 1, ERROR_MESSAGE);
            exit(1);
        }
    }

    for (int i = 1; i < numArgs; i++) {
        long argNum = strtol(args[i], NULL, 10);
        if (argNum <= 0) {
            printf("Invalid size given for one or more arguments, should be greater than 0\n%s", ERROR_MESSAGE);
            exit(1);
        }
    }

    // Converting args to int array
    int *validArgs = malloc(sizeof(int) * 4);
    for (int i = 1; i < numArgs; i++)
        validArgs[i-1] = (int) strtol(args[i], NULL, 10);

    // Too many players for the board
    if ((validArgs[0] + validArgs[1]) > (validArgs[2] * validArgs[3])) {
        printf("The number of players exceeds the allowed space on the game board \n"
        "%d players specified but only %d spaces availible\n%s", validArgs[0] + validArgs[1], validArgs[2] * validArgs[3], ERROR_MESSAGE);
        exit(1);
    }

    return validArgs;
}

/* 
*  Generates the map initially to 0's for unoccupied then populates
*  Team A and Team B members in random locations without overlapping.
*  Saves original bases indicies to memory as a reference to not change later.
*/
void generateMap(int* gameArgs, int* resBases, FILE* fptr) {
    int rows = gameArgs[2];
    int columns = gameArgs[3];
    int numSpaces = rows * columns;
    int numTeamA = gameArgs[0];
    int numTeamB = gameArgs[1];
    srand(time(NULL)); // Sets random seed once for efficiency

    // Initializing Map spaces to 0 for 'unoccupied'
    unsigned char buffer[numSpaces];
    for (int i = 0; i < numSpaces; i++) {
        buffer[i] = 0;
    }

    while (numTeamA > 0) {
        int loc = rand() % numSpaces;
        if (buffer[loc] == 0) {
            buffer[loc] = 0xA;
            --numTeamA;
        }
    }

    while (numTeamB > 0) {
        int loc = rand() % numSpaces;
        if (buffer[loc] == 0) {
            buffer[loc] = 0xB;
            --numTeamB;
        }
    }

    // Saving the indicies of reserved (non-conquerable) bases
    int resIndex = 0;
    for (int i = 0; i < numSpaces; i++) {
        if (buffer[i] != 0) {
            resBases[resIndex] = i;
            ++resIndex;
        }
    }

    fwrite(buffer, sizeof(unsigned char), numSpaces, fptr);
}

// The supervisor thread process which signals to others the game has ended
void* supervisorThread(void* arg) {

    struct processMemory *pMem = (struct processMemory *) arg;
    unsigned char buffer[pMem->numSpaces];

    printf("I am Supervisor thread [#%ld] \n", (long)pthread_self());
    fflush(stdout);

    while (endGame != 1) {

        pthread_mutex_lock(&mLock);

            fseek(pMem->mFile, 0, SEEK_SET);
            fread(buffer, sizeof(buffer), 1, pMem->mFile);

        pthread_mutex_unlock(&mLock);

        int numOccupied = 0;

        for (int i = 0; i < pMem->numSpaces; i++) {
            if (buffer[i] != 0) {
                ++numOccupied;
            }
        }

        if (numOccupied == pMem->numSpaces)
            endGame = 1;
    }

    return (void*)0;
}

// Team member missile firing thread function
void* fireMissile(void* arg) {

    struct processMemory *pMem = (struct processMemory *) arg;
    unsigned char buffer[pMem->numSpaces];

    printf("I am thread [#%ld] \n", (long)pthread_self());
    fflush(stdout);
    
    // As long as supervisor thread has not issued exit
    while (endGame != 1) {

        // Generate Missile coordinate
        int missileCoor = rand() % pMem->numSpaces;

        // Only one process at a time can write to the map file (for testing)
        pthread_mutex_lock(&mLock);

            // for 3 rows (need to calculate locations to move to)
                // Seek the start of the lock region? (3x3 matrix range)
                // read in 3 values

            // buffer is of size 9 and now contains region

            // use pread() and pwrite() and lseek() for efficiency and less sys calls?

        pthread_mutex_unlock(&mLock);
    }

    return (void*)0;
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        printf("Insufficient number of arguments!\nA file name must be provided after the program name.\n%s", ERROR_MESSAGE);
        return 1;
    }

    else if (argc > 5) {
        printf("Too many arguments given\nUsage:\n<program name> <# Team A members> <# Team B members> <# Rows> <# Columns>\n%s", ERROR_MESSAGE);
        return 1;
    }

    else {
        FILE *mapFile = fopen("mapFile.bin", "wb+");

        if (!mapFile) {
            printf("An error occured trying to create the map binary file\n%s", ERROR_MESSAGE);
            return 1;
        }
        else {
            printf("Successfully created new binary file\n");
            
            int *gameArgs = checkCommand(argc, argv);
            int rows = gameArgs[2];
            int columns = gameArgs[3];
            int numSpaces = rows * columns;
            int numResBases = gameArgs[0] + gameArgs[1];
            
            // Holds the original bases which cannot be destroyed/conquered
            int *reservedBases = malloc(numResBases * sizeof(int));
            generateMap(gameArgs, reservedBases, mapFile);

            // PRINT FILE CONTENTS AFTER INITIALIZATION
            unsigned char resbuffer[numSpaces];
            fseek(mapFile, 0, SEEK_SET);
            fread(resbuffer, sizeof(resbuffer), 1, mapFile);

            printf("\n");
            for (int i = 0; i < numSpaces; i++) {
                printf("%x ", resbuffer[i]);
                
                if ((i + 1) % columns == 0)
                    printf("\n");
            }

            // [ ----- Thread town ----- ]
            // Allocate threads for team A & B members
            srand(time(NULL)); // Generate new seed for missile locations (once for efficiency)
            pthread_t supervisor;
            pthread_t teamA[gameArgs[0]];
            pthread_t teamB[gameArgs[1]];
            int sVal;
            int aVal;
            int bVal;

            // Initializing process memory to pass for threads to use
            struct processMemory pMem;
            pMem.mFile = mapFile;
            pMem.numSpaces = numSpaces;
            pMem.rows = rows;
            pMem.cols = columns;

            printf("Starting thread executions\n");
            sVal = pthread_create(&supervisor, NULL, supervisorThread, &pMem);

            for (int i = 0; i < gameArgs[0]; i++)
                aVal = pthread_create(&teamA[i], NULL, fireMissile, &pMem);

            for (int i = 0; i < gameArgs[1]; i++)
                bVal = pthread_create(&teamB[i], NULL, fireMissile, &pMem);

            for (int i = 0; i < gameArgs[0]; i++)
                pthread_join(teamA[i], NULL);
            
            for (int i = 0; i < gameArgs[0]; i++)
                pthread_join(teamB[i], NULL);

            pthread_join(supervisor, NULL);

            // [ ----- Deallocations ----- ]
            free(reservedBases);
            reservedBases = NULL;

            free(gameArgs);
            gameArgs = NULL;
        }

        int fErrStatus = fclose(mapFile);

        if (fErrStatus != 0)
            printf("%s\n", ERROR_MESSAGE);

        return fErrStatus;
    }
}