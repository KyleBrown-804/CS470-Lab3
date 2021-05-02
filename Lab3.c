#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#define ERROR_MESSAGE "The program exited with code 1\n\n"

struct teammate {
    int xcoor, ycoor;
    char teamSide;
};

void *teamMember(void *arg) {
    struct teammate *tMem = (struct teammate *)arg;
}

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

// this function is from:
// https://stackoverflow.com/questions/1202687/how-do-i-get-a-specific-range-of-numbers-from-rand
int genRandom(int min, int max){
   srand(time(NULL));
   return min + rand() / (RAND_MAX / (max - min + 1) + 1);
}

void writeToBinary(int rows, int cols, FILE *fptr, unsigned char **board) {
    fseek(fptr, 0, SEEK_SET);

    for (int i = 0; i < rows; i++) {
        fwrite(board[i], sizeof(unsigned char *), cols, fptr);
    }
    
}

/* Generates the map initially to 0's for unoccupied then populates
*  Team A and Team B members in random locations without overlapping.
*/
void generateMap(int* gameArgs, int numSpaces, FILE *fptr, unsigned char **board) {
    
    int rows = gameArgs[2];
    int columns = gameArgs[3];

    // Initializing Map spaces to 0 for 'unoccupied'
    unsigned char buffer[numSpaces];
    for (int i = 0; i < numSpaces; i++) {
        buffer[i] = 0;
    }

    fwrite(buffer, sizeof(buffer), 1, fptr);

    // Generates random locations for Team A
    int numTeamA = gameArgs[0];
    while (numTeamA > 0) {
        int xcoor = genRandom(0, (rows - 1)); // 0 <= i < M
        int ycoor = genRandom(0, (columns - 1)); // 0 <= j < N
        
        if (board[xcoor][ycoor] == 0) {
            board[xcoor][ycoor] = 0xa;
            numTeamA -= 1;
        }
    }

    // Generates random locations for Team B
    int numTeamB = gameArgs[1];
    while (numTeamB > 0) {
        int xcoor = genRandom(0, (rows - 1)); // 0 <= i < M
        int ycoor = genRandom(0, (columns - 1)); // 0 <= j < N
        
        if (board[xcoor][ycoor] == 0) {
            board[xcoor][ycoor] = 0xb;
            numTeamB -= 1;
        }
    }

    writeToBinary(rows, columns, fptr, board);


    printf("Wrote 2D array to binary file!\n");

    // PRINTING 2D ARRAY TESTING BOARD
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < columns; j++) {
            printf("%x ", board[i][j]);
                    
            if (j+1 == columns)
                printf("\n");
        }
    }

}

// Command line arguments are:
// #TeamA, #TeamB, M-rows, N-columns
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
            
            // 2D array used to display the simulation in standard output
            // https://www.geeksforgeeks.org/dynamically-allocate-2d-array-c/ 
            unsigned char **gameBoard = (unsigned char **) malloc(rows * sizeof(unsigned char *));
            for (int i = 0; i < rows; i++)
                gameBoard[i] = (unsigned char *) malloc(columns * sizeof(unsigned char *));

            for (int i = 0; i < rows; i++)
                for (int j = 0; j < columns; j++)
                    gameBoard[i][j] = 0;

            generateMap(gameArgs, numSpaces, mapFile, gameBoard);


            // RESET FILE POINTER AND TEST READING
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
            pthread_t teamA[gameArgs[0]];
            pthread_t teamB[gameArgs[1]];

            // [ ----- Deallocations ----- ]
            for (int i = 0; i < rows; i++) {
                free(gameBoard[i]);
                gameBoard[i] = NULL;
            }

            free(gameBoard);
            gameBoard = NULL;
            
            free(gameArgs);
            gameArgs = NULL;
        }

        int fErrStatus = fclose(mapFile);

        if (fErrStatus != 0)
            printf("%s\n", ERROR_MESSAGE);

        return fErrStatus;
    }
}