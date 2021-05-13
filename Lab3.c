/*
* Kyle Brown
* 5/12/2021
* CS470 Operating Systems
*/
#define _GNU_SOURCE // Need to define at the top in order to use OFD (open file descriptor) locks
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <stdatomic.h>

// Used so frequently it makes sense to declare them once
#define ERROR_MESSAGE "The program exited with code 1\n\n"

atomic_int endGame = 0;
pthread_mutex_t mLock;
int numSpaces;
int rows;
int cols;
unsigned char *endResults;

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
    for (int i = 0; i < numSpaces; i++) {
        if (resbuffer[i] == 0) {
            printf("%x%x ", 0, 0);
            fflush(stdout);
        } else {
            printf("%x ", resbuffer[i]);
            fflush(stdout);
        }

        if ((i + 1) % cols == 0) {
            printf("\n");
            fflush(stdout);
        }
    }

}

// Prints results at the very end of execution to avoid late
// threads to print after the supervisor thread
void printEndResults() {

    // Counts spaces occupied by Team A and B for who won
    int numA = 0, numB = 0;
    for (int i = 0; i < endResults[i]; i++) {
        if (endResults[i] == 0xaa || endResults[i] == 0xaf)
            ++numA;
        else if (endResults[i] == 0xbb || endResults[i] == 0xbf)
            ++numB;
    }

    printf("\n[ =============== GAME OVER =============== ]\n");

    if (numA == numB) {
        printf("DRAW - Team A and B share an equal number of territory!\n");
    }
    else if (numA > numB) {
        printf("VICTORY TEAM A - Team A holds more territory and conquered Team B!\n");
    }
    else {
        printf("VICTORY TEAM B - Team B holds more territory and conquered Team A!\n");
    }
    printf("[ ========================================= ]\n\n");
    printf("[ =============== ENDING BOARD =============== ]\n\n");

    for (int i = 0; i < numSpaces; i++) {
        if (endResults[i] == 0) {
            printf("%x%x ", 0, 0);
        } else {
            printf("%x ", endResults[i]);
        }

        if ((i + 1) % cols == 0) {
            printf("\n");
        }
    }

    printf("[ ============================================= ]\n\n");
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
    // that the enemy doesn't take majority if they weren't the one firing
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

    pwrite(mapFd, &buffer, numSpaces, 0);
}

// The supervisor thread process which signals to others the game has ended
void* supervisorThread(void* arg) {
    unsigned char buffer[numSpaces];
    int superFd = open("mapFile.bin", O_RDWR);

    while (endGame != 1) {
        pread(superFd, &buffer, numSpaces, 0);

        int numOccupied = 0;
        for (int i = 0; i < numSpaces; i++) {
            if (buffer[i] != 0) {
                ++numOccupied;
            }
        }

        if (numOccupied == numSpaces)
            endGame = 1;
    }

    // Copies end results over to be printed after all other threads finish
    endResults = malloc(numSpaces * sizeof(unsigned char));
    for (int i = 0; i < numSpaces; i++) {
        endResults[i] = buffer[i];
    }

    int superStatus = close(superFd);
    return (void*)0;
}

// Team member missile firing thread function:
// uses a combination of mutex locking and "open file descriptor" (OFD) locks
// in order to allow parallel thread execution to different byte ranges of a file.
void* fireMissile(void* arg) {

    // [NOTE] this function is longer solely because threads need multiple byte
    // range section locks (2 or 3 per thread) in order to lock bomb range "vicinities"
    // correctly since the byte ranges actually exist on a 1D buffer array from reading
    // in from a file, not a 2D one as the board is conceptually thought of and displayed as.

    struct processMem *pMem = (struct processMem *) arg;

    // As long as supervisor thread has not issued exit signal, keeps looping
    while (endGame != 1) {

        int threadFd = open("mapFile.bin", O_RDWR);

        // Generate Missile coordinate
        int missileCoor = rand() % numSpaces;

        // Pre-Checks needed section size and indices
        int *bombRange = checkCriticalSection(missileCoor, pMem, threadFd);

        // Case where missile hit a non destroyable/conquerable space
        if (bombRange == NULL) {
            printf("\n***MISSILE FAILED*** to blow up original base at index %d\n", missileCoor);
            fflush(stdout);
            pMem->critSectionSize = 0;
            continue;
        }

        // Setting up locking file section lock range (at most requires 3 locks per thread)
        struct flock toplock;
        struct flock midlock;
        struct flock bottomlock;
        int topEnd = 0, midStart = 0, midEnd = 0, bottomStart = 0;
        int topStart = bombRange[0];
        int bottomEnd = bombRange[pMem->critSectionSize-1];

        // Gets the number of rows the bomb vicinity occupies
        int topRow = bombRange[0] / cols;
        int bottomRow = bombRange[pMem->critSectionSize-1] / cols;
        int numRows = (bottomRow - topRow) + 1;

        // On any sized grid there are only ever 2 or 3 rows
        if (numRows == 2) {
            for (int i = 1; i < pMem->critSectionSize; i++) {
                if ((bombRange[i] / cols) > topRow) {
                    topEnd = bombRange[i-1];
                    bottomStart = bombRange[i];
                    break;
                }
            }
        } else {
            int midRow = topRow+1;
            for (int i = 1; i < pMem->critSectionSize; i++) {
                if ((bombRange[i] / cols) == midRow) {
                    midStart = bombRange[i];
                    topEnd = bombRange[i-1];
                    break;
                }
            }
            for (int i = 1; i < pMem->critSectionSize; i++) {
                if ((bombRange[i] / cols) == bottomRow) {
                    bottomStart = bombRange[i];
                    midEnd = bombRange[i-1];
                    break;
                }
            }
        }

        // Sets byte ranges for each row [start, end] such as [0, 2]
        toplock.l_whence = SEEK_SET;
        toplock.l_start = topStart;
        toplock.l_len = topEnd;
        toplock.l_pid = 0;
        bottomlock.l_whence = SEEK_SET;
        bottomlock.l_start = bottomStart;
        bottomlock.l_len = bottomEnd;
        bottomlock.l_pid = 0;

        if (numRows == 3) {
            midlock.l_whence = SEEK_SET;
            midlock.l_start = midStart;
            midlock.l_len - midEnd;
            midlock.l_pid = 0;
        }

        // The open file descriptors must be mutex locked to avoid deadlocks with multiple locks per thread
        pthread_mutex_lock(&mLock);

            // Locking the critical section for reading/writing to a section (sectioned file locking by byte range)
            // Reference on why the new "F_OFD_SETLKW" works for multi-threading while the old posix version does
            // not: https://gavv.github.io/articles/file-locks/#open-file-description-locks-fcntl

            // more info / proof this works for multi-threading:
            // https://www.gnu.org/software/libc/manual/html_node/Open-File-Description-Locks.html#Open-File-Description-Locks

            // [NOTE] F_OFD_SETLFW forces a thread needing access to the same section
            // to wait meanwhile non-conflicting sections may continue.
            toplock.l_type = F_WRLCK;
            bottomlock.l_type = F_WRLCK;
            fcntl(threadFd, F_OFD_SETLKW, &toplock);
            fcntl(threadFd, F_OFD_SETLKW, &bottomlock);

            if (numRows == 3) {
                midlock.l_type = F_WRLCK;
                fcntl(threadFd, F_OFD_SETLKW, &midlock);
            }

            // [ --- FILE SECTION LOCKED --- ]
            // now a given thread is OFD locked meaning other threads which try to use
            // fcntl() on the same byte section will be blocked and must wait for that section
        pthread_mutex_unlock(&mLock);

        // Reading in vicinity and then writing the contents of updated vicinity back into
        // the critical section of the file
        unsigned char *resultBuffer = readVicinity(missileCoor, pMem, bombRange, threadFd);
        for (int i = 0; i < pMem->critSectionSize; i++) {
            lseek(threadFd, bombRange[i], SEEK_SET);
            write(threadFd, &resultBuffer[i], 1);
        }

        printf("\n*** MISSILE HIT *** at index %d\n", missileCoor);
        fflush(stdout);
        printFile(threadFd);

        // Clears section size for next iteration
        pMem->critSectionSize = 0;

        // [ ----- Deallocations ----- ]
        free(bombRange);
        bombRange = NULL;
        free(resultBuffer);
        resultBuffer = NULL;

        // Unlocking the file section and closing the file descriptor instance
        toplock.l_type = F_UNLCK;
        bottomlock.l_type = F_UNLCK;
        fcntl (threadFd, F_OFD_SETLK, &toplock);
        fcntl (threadFd, F_OFD_SETLK, &bottomlock);

        if (numRows == 3) {
            midlock.l_type = F_UNLCK;
            fcntl (threadFd, F_OFD_SETLK, &midlock);
        }

        int threadFdStatus = close(threadFd);

        // Allows other threads to be chosen more often from the ready queue
        // Also allows good visual representation of each threads execution
        // This is 1/4 of a second, increase to slow down printing if needed
        struct timespec threadWait;
        threadWait.tv_nsec = 250000000L;
        nanosleep(&threadWait, NULL);
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
        // Handles issues that open() has with overwriting an existing file of the same name
        FILE *mapFile = fopen("mapFile.bin", "wb+");
        fclose(mapFile);

        int mapFd = open("mapFile.bin", O_RDWR);
        if (mapFd == -1) {
            printf("An error occured trying to create the map binary file\n%s", ERROR_MESSAGE);
            return 1;
        }
        else {
            printf("[ ----- Starting the game! ----- ]\n\n");

            int *gameArgs = checkCommand(argc, argv);
            rows = gameArgs[2];
            cols = gameArgs[3];
            numSpaces = rows * cols;

            generateMap(gameArgs, mapFd);

            // Prints intial game board with original non-changeable bases
            unsigned char resbuffer[numSpaces];
            pread(mapFd, &resbuffer, numSpaces, 0);
            for (int i = 0; i < numSpaces; i++) {
                printf("%x ", resbuffer[i]);

                if ((i + 1) % cols == 0)
                    printf("\n");
            }

            // Allocate threads for team A & B members and supervisor
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

            printEndResults();
            free(endResults);
            endResults = NULL;
        }

        int mapCloseStatus = close(mapFd);
        if (mapCloseStatus == -1)
            printf("%s\n", ERROR_MESSAGE);

        return mapCloseStatus;
    }
}