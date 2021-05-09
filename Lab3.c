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
    // unsigned char teamSign;
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

    // Make sure columns > 1 

    return validArgs;
}

// Checks if space in vacinity is valid concerning edges and corners
int isValidSpace(int misX, int misY, int cols, int otherLoc) {
    int xCoor = otherLoc % cols;
    int yCoor = otherLoc / cols;

    // All vicinity range should be at most 1 away from missile landing
    if (abs(misX - xCoor) <= 1 && abs(misY - yCoor) <= 1)
        return 1;
    else
        return 0;
}

// Handles reading in the vicinity during a missile strike
unsigned char *readVicinity(int misLoc, int cols, int rows, unsigned char team, FILE *fptr) {
    
    unsigned char blastLoc;
    unsigned char enemy;
    fseek(fptr, misLoc, SEEK_SET);
    fread(&blastLoc, sizeof(unsigned char), 1, fptr);
    
    if (team == 0xaa || team == 0xaf)
        enemy = 0xbb;
    else
        enemy = 0xaa;

    // Checks if blast spot is non-conquerable and returns since the missile fails
    // otherwise assigns to appropriate team or relinquishes if friendly fire
    if (blastLoc == 0xaf || blastLoc == 0xbf)
        return NULL;
    else if (blastLoc == team)
        blastLoc = 0;
    else
        blastLoc = team;
    
    // [ ---------- Start of vicinity checking ---------- ]
    int vicinity[] = {(misLoc-cols)-1, misLoc-cols, (misLoc-cols)+1, misLoc-1, misLoc, misLoc+1, (misLoc+cols)-1, misLoc+cols, (misLoc+cols)+1};
    int numSpaces = cols * rows;
    int xCoor = misLoc % cols;
    int yCoor = misLoc / cols;
    int safeLocs = 0;
        
    for (int i = 0; i < 9; i++) {
        if (vicinity[i] < 0 || vicinity[i] >= numSpaces)
            vicinity[i] = -1;
        else if (! isValidSpace(xCoor, yCoor, cols, vicinity[i]))
            vicinity[i] = -1;
        else if (i > 0) {
            // Edge case for handling skinny arrays such as 2 width
            if (vicinity[i] == vicinity[i-1])
                vicinity[i] = -1;
            else
                safeLocs++;
        }
        else
            safeLocs++;
    }

    int bombRange[safeLocs];
    for (int i = 0, j = 0; i < 9; i++) {
        if (vicinity[i] != -1) {
            bombRange[j] = vicinity[i];
            j++;
        }
    }

    // [NOTE] ONLY READS IN CRITICAL SECITON TO AVOID OVERLAP DEADLOCKS
    // For all safe spaces to index, loads in bytes from file into bomb range
    unsigned char *buffer = malloc(safeLocs * sizeof(unsigned char));
    for (int i = 0; i < safeLocs; i++) {
        if (bombRange[i] == misLoc) {
            buffer[i] = blastLoc;
            continue;
        }
        fseek(fptr, bombRange[i], SEEK_SET);
        fread(&buffer[i], sizeof(unsigned char), 1, fptr);
    }

    // Counts to determine if either team is routed (overrun) by majority team
    int numA = 0, numB = 0, unocc = 0;
    for (int i = 0; i < safeLocs; i++) {
        if (buffer[i] == 0xaa || buffer[i] == 0xaf)
            numA++;
        else if (buffer[i] == 0xbb || buffer[i] == 0xbf)
            numB++;
        else
            unocc++;
    }

    printf("Num A: %d\t Num B: %d\t Num unoccupied %d\n", numA, numB, unocc);

    // Returns early if both teams have equal hold
    if (numA == numB)
        return buffer;

    // Both checks are made to ONLY route the enemy if the
    // majority around (k, l) and (k, l) itself are now occupied
    // by the "team who sent the missile". The logic below assures 
    // that the enemy doesn't take majority if they weren't firing
    if (team == 0xaa || team == 0xaf) {
        if (numA > numB) {
            for (int i = 0; i < safeLocs; i++) {
                if (buffer[i] != 0xaf && buffer[i] != 0xbf) {
                    buffer[i] = team;
                }
            }
        }
    }
    else if(team == 0xbb || team == 0xbf) {
        if (numB > numA) {
            for (int i = 0; i < safeLocs; i++) {
                if (buffer[i] != 0xaf && buffer[i] != 0xbf) {
                    buffer[i] = team;
                }
            }
        }
    }
    return buffer;
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
            buffer[loc] = 0xaf;
            --numTeamA;
        }
    }

    while (numTeamB > 0) {
        int loc = rand() % numSpaces;
        if (buffer[loc] == 0) {
            buffer[loc] = 0xbf;
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

        // Sleeps the supervisor thread for half a second to allow
        // the other threads more schedule time
        struct timespec superWait;
        superWait.tv_nsec = 500000000L;
        nanosleep(&superWait, NULL);
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

        // Should add logic to check critical section area needed first
        // [checkVicinity() function here]

        // Only one process at a time can write to the map file (for testing)
        pthread_mutex_lock(&mLock);

            // unsigned char *resBuffer= readVicinity(missileCoor, pMem->cols, pMem->rows, pMem->teamSign, pMem->mFile);

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