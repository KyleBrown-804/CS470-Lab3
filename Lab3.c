#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <stdatomic.h>

#define ERROR_MESSAGE "The program exited with code 1\n\n"

atomic_int endGame = 0;
pthread_mutex_t mLock;
int numSpaces;
int rows;
int cols;

// Process memory to share with the current process's multiple threads
struct processMem {
    unsigned char teamSign;
    int critSectionSize;
};

// Checks for valid logical inputs in command line args
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

    // Checks if board dimensions are too small to feasibly work with
    if (validArgs[2] < 2 || validArgs[3] < 2) {
        printf("The dimensions specified are too small, rows and columns must be at least size 2\n%s", ERROR_MESSAGE);
        exit(1);
    }

    return validArgs;
}

// Prints the file contents to the terminal given a file descriptor
void printFile(int fdesc) {
    unsigned char resbuffer[numSpaces];
    pread(fdesc, &resbuffer, numSpaces, 0);

    printf("\n");
    fflush(stdout);
    for (int i = 0; i < numSpaces; i++) {
        printf("%x ", resbuffer[i]);
        fflush(stdout);
        
        if ((i + 1) % cols == 0) {
            printf("\n");
            fflush(stdout);
        }
    }
}

// Checks if space in vacinity is valid concerning edges and corners
int isValidSpace(int misX, int misY, int otherLoc) {
    int xCoor = otherLoc % cols;
    int yCoor = otherLoc / cols;

    // All vicinity range should be at most 1 away from missile landing
    if (abs(misX - xCoor) <= 1 && abs(misY - yCoor) <= 1)
        return 1;
    else
        return 0;
}

// Handles reading in the vicinity during a missile strike
unsigned char *readVicinity(int misLoc, struct processMem *pMem, int *bombRange, int threadFd) {
    unsigned char blastLoc;
    unsigned char enemy;
    pread(threadFd, &blastLoc, 1, misLoc);
    if (pMem->teamSign == 0xaa)
        enemy = 0xbb;
    else
        enemy = 0xaa;

    // Assigns to appropriate team or relinquishes if friendly fire
    if (blastLoc == pMem->teamSign)
        blastLoc = 0;
    else if (blastLoc == enemy || blastLoc == 0)
        blastLoc = pMem->teamSign;
    
    // [NOTE] ONLY READS IN CRITICAL SECITON TO AVOID OVERLAP DEADLOCKS
    // For all safe spaces to index, loads in bytes from file into bomb range
    unsigned char *buffer = malloc((pMem->critSectionSize) * sizeof(unsigned char));
    for (int i = 0; i < pMem->critSectionSize; i++) {
        if (bombRange[i] == misLoc) {
            buffer[i] = blastLoc;
            continue;
        }
        pread(threadFd, &buffer[i], 1, bombRange[i]);
    }

    // Counts to determine if either team is routed (overrun) by majority team
    int numA = 0, numB = 0, unocc = 0;
    for (int i = 0; i < pMem->critSectionSize; i++) {
        if (buffer[i] == 0xaa || buffer[i] == 0xaf)
            numA++;
        else if (buffer[i] == 0xbb || buffer[i] == 0xbf)
            numB++;
        else
            unocc++;
    }

    // Returns early if both teams have equal hold
    if (numA == numB)
        return buffer;

    // Both checks are made to ONLY route the enemy if the
    // majority around (k, l) and (k, l) itself are now occupied
    // by the "team who sent the missile". The logic below assures 
    // that the enemy doesn't take majority if they weren't firing
    if (pMem->teamSign == 0xaa) {
        if (numA > numB) {
            for (int i = 0; i < pMem->critSectionSize; i++) {
                if (buffer[i] != 0xaf && buffer[i] != 0xbf) {
                    buffer[i] = pMem->teamSign;
                }
            }
        }
    }
    else if(pMem->teamSign == 0xbb) {
        if (numB > numA) {
            for (int i = 0; i < pMem->critSectionSize; i++) {
                if (buffer[i] != 0xaf && buffer[i] != 0xbf) {
                    buffer[i] = pMem->teamSign;
                }
            }
        }
    }
    return buffer;
}

// Does a precheck of the range needed for the critical section read/writes
int *checkCriticalSection(int misLoc, struct processMem *pMem, int threadFd) {
    // Preemptive check to see if missile lands on a non-conquerable space
    unsigned char blastLoc;
    pread(threadFd, &blastLoc, 1, misLoc);
    if (blastLoc == 0xaf || blastLoc == 0xbf)
        return NULL;
    
    int vicinity[] = {(misLoc-cols)-1, misLoc-cols, (misLoc-cols)+1, misLoc-1, misLoc, misLoc+1, (misLoc+cols)-1, misLoc+cols, (misLoc+cols)+1};
    int xCoor = misLoc % cols;
    int yCoor = misLoc / cols;
    int safeLocs = 0;
        
    for (int i = 0; i < 9; i++) {
        if (vicinity[i] < 0 || vicinity[i] >= numSpaces)
            vicinity[i] = -1;
        else if (! isValidSpace(xCoor, yCoor, vicinity[i]))
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

    // Allocates the size needed for the critical section and an 
    // array of locations affected within the crticial section
    int *bombRange = malloc(safeLocs * sizeof(int));
    pMem->critSectionSize = safeLocs;

    for (int i = 0, j = 0; i < 9; i++) {
        if (vicinity[i] != -1) {
            bombRange[j] = vicinity[i];
            j++;
        }
    }

    return bombRange;
}

// Generates the map initially to 0's for unoccupied then populates
// Team A and Team B members in random locations without overlapping.
void generateMap(int* gameArgs, int mapFd) {
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

    //fwrite(buffer, sizeof(unsigned char), numSpaces, fptr);
    pwrite(mapFd, &buffer, numSpaces, 0);
}

// The supervisor thread process which signals to others the game has ended
void* supervisorThread(void* arg) {

    //struct processMem *pMem = (struct processMem *) arg;
    unsigned char buffer[numSpaces];
    int superFd = open("mapFile.bin", O_RDWR);

    printf("I am Supervisor thread [#%ld] \n", (long)pthread_self());
    fflush(stdout);

    while (endGame != 1) {

        pthread_mutex_lock(&mLock);

            pread(superFd, &buffer, numSpaces, 0);

        pthread_mutex_unlock(&mLock);

        int numOccupied = 0;

        for (int i = 0; i < numSpaces; i++) {
            if (buffer[i] != 0) {
                ++numOccupied;
            }
        }

        if (numOccupied == numSpaces)
            endGame = 1;

        
        printf("\n[SUPERVISOR] -- spaces left: %d\n", numSpaces-numOccupied);
        fflush(stdout);

        // Sleeps the supervisor thread for half a second to allow
        // the other threads more schedule time (better for smaller grids)
        struct timespec superWait;
        superWait.tv_nsec = 500000000L;
        nanosleep(&superWait, NULL);
    }

    // After breaking the loop the game has been one
    int numA = 0, numB = 0;
    for (int i = 0; i < buffer[i]; i++) {
        if (buffer[i] == 0xaa || buffer[i] == 0xaf)
            ++numA;
        else if (buffer[i] == 0xbb || buffer[i] == 0xbf)
            ++numB;
    }

    printf("\n[ =============== GAME OVER =============== ]\n");

    if (numA == numB)
        printf("DRAW - Team A and B share an equal number of territory!\n");
    else if (numA > numB)
        printf("VICTORY TEAM A - Team A holds more territory and conquered Team B!\n");
    else
        printf("VICTORY TEAM B - Team B holds more territory and conquered Team A!\n");

    printf("\nEnding Game Board:\n");
    fflush(stdout);
    printFile(superFd);
    printf("[ ========================================= ]\n\n");

    int superStatus = close(superFd);
    return (void*)0;
}

// Team member missile firing thread function
void* fireMissile(void* arg) {

    struct processMem *pMem = (struct processMem *) arg;
    unsigned char buffer[numSpaces];
    int threadFd = open("mapFile.bin", O_RDWR);

    printf("I am thread [#%ld] \n", (long)pthread_self());
    fflush(stdout);
    
    // As long as supervisor thread has not issued exit
    while (endGame != 1) {

        // Generate Missile coordinate
        int missileCoor = rand() % numSpaces;

        // Checks needed section size and indices
        int *bombRange = checkCriticalSection(missileCoor, pMem, threadFd);

        // Case where missile hit a non destroyable/conquerable space
        if (bombRange == NULL) {
            printf("***MISSILE FAILED*** to blow up original base at index %d\n", missileCoor);
            fflush(stdout);
            pMem->critSectionSize = 0;
            continue;
        }

        // Locking critical section for reading/writing to section
        // [Note] must lock for reading to avoid overlapping critical sections
        // causing deadlocks when one team overwhelms the enemy and claims the section
        pthread_mutex_lock(&mLock);
            unsigned char *resBuffer = readVicinity(missileCoor, pMem, bombRange, threadFd);

            // Writing the contents of results buffer back into the critical section of the file
            for (int i = 0; i < pMem->critSectionSize; i++) {
                pwrite(threadFd, &resBuffer[i], 1, bombRange[i]);
            }

            printf("***MISSILE HIT*** at index %d\n", missileCoor);
            fflush(stdout);
            printFile(threadFd);

            // clear critical section holding
            pMem->critSectionSize = 0;

            // Deallocations
            free(bombRange);
            bombRange = NULL;

            free(resBuffer);
            resBuffer = NULL;
        pthread_mutex_unlock(&mLock);

        // For Testing shows that each thread executes correctly
        struct timespec threadWait;
        threadWait.tv_nsec = 500000000L;
        nanosleep(&threadWait, NULL);
    }

    int threadFdStatus = close(threadFd);

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
        // Handles issues that open() has with overwriting an existing file of the same name
        FILE *mapFile = fopen("mapFile.bin", "wb+");
        fclose(mapFile);

        int mapFd = open("mapFile.bin", O_RDWR);

        if (mapFd == -1) {
            printf("An error occured trying to create the map binary file\n%s", ERROR_MESSAGE);
            return 1;
        }
        else {
            
            printf("Successfully created new binary file\n");
            
            int *gameArgs = checkCommand(argc, argv);
            rows = gameArgs[2];
            cols = gameArgs[3];
            numSpaces = rows * cols;
            
            generateMap(gameArgs, mapFd);

            // PRINT FILE CONTENTS AFTER INITIALIZATION
            unsigned char resbuffer[numSpaces];
            pread(mapFd, &resbuffer, numSpaces, 0);

            printf("\n");
            for (int i = 0; i < numSpaces; i++) {
                printf("%x ", resbuffer[i]);
                
                if ((i + 1) % cols == 0)
                    printf("\n");
            }

            // Allocate threads for team A & B members
            pthread_t supervisor;
            pthread_t teamAThreads[gameArgs[0]];
            pthread_t teamBThreads[gameArgs[1]];
            int sVal = 0, aVal = 0, bVal = 0;

            // Initializing process memory to pass for threads to use
            struct processMem superMem;
            struct processMem *teamAMem = malloc(gameArgs[0] * sizeof(*teamAMem));
            struct processMem *teamBMem = malloc(gameArgs[1] * sizeof(*teamBMem));

            for (int i = 0; i < gameArgs[0]; i++)
                teamAMem[i].teamSign = 0xaa;

            for (int i = 0; i < gameArgs[1]; i++)
                teamBMem[i].teamSign = 0xbb;

            // Generates new seed for missile locations in thread functions
            srand(time(NULL));

            // Creating/Joining of supervisor and worker threads
            sVal = pthread_create(&supervisor, NULL, supervisorThread, NULL);

            for (int i = 0; i < gameArgs[0]; i++)
                aVal = pthread_create(&teamAThreads[i], NULL, fireMissile, &teamAMem[i]);

            for (int i = 0; i < gameArgs[1]; i++)
                bVal = pthread_create(&teamBThreads[i], NULL, fireMissile, &teamBMem[i]);

            for (int i = 0; i < gameArgs[0]; i++)
                pthread_join(teamAThreads[i], NULL);
            
            for (int i = 0; i < gameArgs[0]; i++)
                pthread_join(teamBThreads[i], NULL);

            pthread_join(supervisor, NULL);

            // [ ----- Deallocations ----- ]
            free(teamAMem);
            teamAMem = NULL;

            free(teamBMem);
            teamBMem = NULL;

            free(gameArgs);
            gameArgs = NULL;
        }

        int mapCloseStatus = close(mapFd);
        
        if (mapCloseStatus == -1)
            printf("%s\n", ERROR_MESSAGE);

        return mapCloseStatus;
    }
}