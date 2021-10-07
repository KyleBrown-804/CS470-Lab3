# CS470-Lab3

[COMPILING AND RUNNING]
    This program was designed to be run a native Linux machine or using 
    Docker with a native Linux image/container. This program will NOT run
    properly using Windows Subsystem for Linux (WSL) or MacOSX. It is 
    VITALY important that this is run on a newer debian based Linux distribution
    such as Ubuntu 20.04 (which it was tested on).

    To compile all that should be needed is GCC an to run the following command:
    
        "gcc -o Lab3 Lab3.c -pthread"
    
[ABOUT FILE SECTION LOCKING]
    This program uses special OFD (open file descriptor) locks with fcntl() in order
    to implement sectioned or "byte range" file locking. These work essentially the
    same as the normal way one would use POSIX flock structs with fcntl() except that
    these OFD types allow thread safety and reliable behavior/locking.

    Information and proof of the differences between these locks and regular can
    be found at these two sources:

    1) https://gavv.github.io/articles/file-locks/#open-file-description-locks-fcntl
    2) https://www.gnu.org/software/libc/manual/html_node/Open-File-Description-Locks.html#Open-File-Description-Locks 

    These locks allow for the desired section locking which I use in junction with mutex locking
    to assure that no threads can aquire the same set of locks and cause any deadlocks

[CONSOLE OUTPUT]
    The assignment specified to make each step of execution traceable so I implemented
    threads printing out the game board after their critical section read/writing updates.
    In addition I included an indication of where each missile landed and if it failed
    (meaning it hit a non-conquerable or "original" base). At the beginning the starting 
    gameboard will be printed to indicate the "original" bases and the end result of the 
    board will be printed at the end of execution. 
    
    Team A is displayed as "0xaa" Team B is "0xbb" and their original bases are "0xaf" and
    "0xbf" accordingly and "0x00" represents unoccupied space.

[RUNTIME]
    Please note that due to random number generation the time that games take to finish is
    non consistant and fluctuates with randomness. This is due to the fact that random coordinates
    are always selected and one of the rules was that bases may blow themselves up if they landed
    on their own base. This can cause the gameboard to be almost completely filled up but then keep
    the program going as the supervisor thread has to keep checking if there are no more unoccupied spaces.

    Note that larger grids and more threads may increase the runtime of the program.

[VICINITY RESULTS]
    Please note that the instructions were followed explicitly and rules 3 and 4 were matched
    literally when it comes to how the vicinity is read and updated. Friendly fire may exist, no
    original bases blow themselves up. 
    
    In addition only the firing team can make a majority check after missile hits and if 
    there are an equal number of Team A and B members then no majority is won.
