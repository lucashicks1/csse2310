#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <csse2310a4.h>
#include <csse2310a3.h>
#include <stdbool.h>
#include <pthread.h>
#include <crypt.h>
#include <ctype.h>
#include <semaphore.h>
#include <signal.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Max and  min values
#define MAX_ARGS 6
#define MAX_WORD_LENGTH 8
#define MIN_PORT 1024
#define MAX_PORT 65535
#define MAX_FIELDS 3
#define CIPHER_LENGTH 13
#define SALT_LENGTH 2
#define MIN_THREADS 1
#define MAX_THREADS 50

// Ascii values for . and / and the numbers 0-9
#define ASCII_MIN 46
#define ASCII_MAX 57

// Responses from server
#define FAILED ":failed"
#define INVALID ":invalid"

// Default dictionary used if none is specified
#define DEFAULT_DICTIONARY "/usr/share/dict/words"
#define STAT_MESSAGE "Connected clients: %u\nCompleted clients: %u\nCrack "\
    "requests: %u\nFailed crack requests: %u\nSuccessful crack requests: %u\n"\
    "Crypt requests: %u\ncrypt()/crypt_r() calls: %u\n"

// Enum to hold exit statuses
typedef enum {
    USAGE_ERROR = 1,
    DICT_FILE_ERROR = 2,
    NO_WORDS_ERROR = 3,
    UNABLE_OPEN_ERROR = 4,
} ExitStatus;

// Struct that holds the information for the server - mostly specified on the
// command line
typedef struct {
    int maxConns;
    const char* portNum;
    char* dictFileName;
} ServerDetails;

// Struct that holds all the stats for the server - this information is printed
// by the thread responsible for SIGHUP handling
typedef struct {
    uint32_t numConnected;
    uint32_t numCompleted;
    uint32_t cracks;
    uint32_t failedCracks;
    uint32_t successCracks;
    uint32_t crypts;
    uint32_t cryptCalls;
    sem_t* lock;
} Statistics;

// Struct that is used to hold the information sent to the thread that handles
// SIGHUPs
typedef struct {
    Statistics* stats;
    sigset_t* set;
} StatsThreadData;

// Dictionary of words with the words and the number of words
typedef struct {
    char** words;
    int numWords;
} Dictionary;

// Client handler thread struct - contains connection fd, dictionary struct
// semaphore pointer to limit connections and stats struct
typedef struct {
    int fd;
    Dictionary* dict;
    sem_t* maxConns;
    Statistics* stats;
} ClientThreadData;

// Struct that contains all of the data sent to each cracking thread
typedef struct {
    char* cipherText;
    char* salt;
    char** words;
    int startPos;
    int endPos;
    volatile int* found;
} CrackThreadData;

// Struct that contains all the information returned by each cracking thread
typedef struct {
    char* word;
    int numCalls;
} CrackThreadReturn;

// Main functions
ServerDetails parse_command_line(int argc, char** argv);
void process_connections(int serv, Dictionary dict, int maxConns);
int open_listen(const char* port);
void process_command(char* command, FILE* out, Dictionary* dict,
        Statistics* stats);

// Client Handler
void* client_wrapper(void* v);
void client_handler_thread(int fd, Dictionary* dict, sem_t* maxConns,
        Statistics* stats);

// Crypt/Crack Calls
char* crypt_call(char* cryptText, char* salt);
char* crack_call(char* cipherText, int numThreads, Dictionary* dict,
        Statistics* stats);
void* crack_thread_wrapper(void* v);
void* crack_thread(void* v);

//Helper prototypes
void validate_port_number(int portNum);
int validate_max_connections(int maxConns);
Dictionary fill_dictionary(char* dictFileName);
void free_dictionary(Dictionary);
int string_to_number(char* arg);
bool valid_thread_num(char* numThreads);
bool valid_salt(char* salt);
bool valid_salt_character(char salt);
CrackThreadData* create_crack_thread_data(char* cipherText, char* salt,
        char** words, int startPos, int endPos, volatile int* found);

// Stats commands
void* stats_thread(void* v);
void stats_add_connection(Statistics* stats);
void stats_complete_connection(Statistics* stats);
void stats_add_crack_request(Statistics* stats);
void stats_add_crack_request_pass(Statistics* stats);
void stats_add_crack_request_fail(Statistics* stats);
void stats_add_crypt_request(Statistics* stats);
void stats_add_crypt_call(Statistics* stats, int num);

// Prototypes for error functions
void usage_error();
void dictionary_error(char* dictName);
void empty_dictionary_error();
void unable_listen_error();

int main(int argc, char** argv) {
    ServerDetails serverDetails;
    Dictionary dictionary;
    int serv;

    serverDetails = parse_command_line(argc, argv);
    dictionary = fill_dictionary(serverDetails.dictFileName);

    // Listens on given port, returns socket for listening
    if ((serv = open_listen(serverDetails.portNum)) < 0) {
        free_dictionary(dictionary);
        unable_listen_error();
    }

    // Processes all incoming client connections
    process_connections(serv, dictionary, serverDetails.maxConns);

    return 0;
}

/* parse_command_line()
 * --------------------
 * Interprets any command line argumens given and parses this information. If
 * this information is valid, it puts this information into a ServerDetails 
 * struct. If incorrect command line argumens were given, it exits the program
 * and prints the required error message.
 *
 * argc: number of arguments passed to the program
 * argv: arguments passed to the program
 *
 * Returns: ServerDetails struct containing server information
 */
ServerDetails parse_command_line(int argc, char** argv) {
    ServerDetails param = {.maxConns = -1, .portNum = NULL, 
        .dictFileName = NULL};
    // Skip program name
    argc--;
    argv++;

    // Check if too many arguments or arguments are missing
    if (argc > MAX_ARGS || argc % 2) {
        usage_error();
    }

    while (argc) {
        // Checks which argument is specified (eg: --maxconn)
        if (strcmp(argv[0], "--maxconn") == 0 && param.maxConns < 0) {
            int maxConns = string_to_number(argv[1]);
            param.maxConns = validate_max_connections(maxConns);
        } else if (strcmp(argv[0], "--port") == 0 && !param.portNum) {
            int portNum = string_to_number(argv[1]);
            validate_port_number(portNum);
            param.portNum = argv[1];
        } else if (strcmp(argv[0], "--dictionary") == 0 
                && !param.dictFileName) {
            param.dictFileName = argv[1];
        } else {
            usage_error(); // If additional or duplicates args are provided
        }
        // Now go check next arg
        argc -= 2;
        argv += 2;
    }
    // Sets portnum to be 0 if not specified
    if (!param.portNum) {
        param.portNum = "0";
    }

    // If not specified, set maxconnections to 0 (no limit)
    if (param.maxConns == -1) {
        param.maxConns = 0;
    }

    // Uses default dictionary if not specified
    if (!param.dictFileName) {
        param.dictFileName = DEFAULT_DICTIONARY;
    }
    return param;
}

/* Listens on a given port and returns a listening socket. If it encounters
 * any errors, it will return with -1. If the port specified is 0, it will
 * use an ephemeral port.
 *
 * port: port which the socket will be bound to
 *
 * Returns: listening socket
 */
int open_listen(const char* port) {
    struct addrinfo* ai = 0;
    struct addrinfo hints;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;   // IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;    // listen on all IP addresses

    int err;
    if ((err = getaddrinfo(NULL, port, &hints, &ai))) {
        freeaddrinfo(ai);
        return -1;   // Could not determine address
    }

    // Create a socket and bind it to a port
    int listenfd = socket(AF_INET, SOCK_STREAM, 0); // 0=default protocol (IP)

    // Allow address (port number) to be reused immediately
    int v = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v)) < 0) {
        return -1;
    }

    if (bind(listenfd, ai->ai_addr, sizeof(struct sockaddr)) < 0) {
        return -1;
    }

    if (listen(listenfd, 128) < 0) {
        return -1;
    }

    // Find out which socket
    struct sockaddr_in ad;
    memset(&ad, 0, sizeof(struct sockaddr_in));
    socklen_t len = sizeof(struct sockaddr_in);
    if (getsockname(listenfd, (struct sockaddr*)&ad, &len)) {
        perror("sockname");
        return -1;
    }
    fprintf(stderr, "%d\n", ntohs(ad.sin_port));
    fflush(stderr);

    // Have listening socket - return it
    return listenfd;
}

/* Configures the statistics struct that is used by the SIGHUP handling thread
 * to print out the server stats. This function sets all of the stats to 0 and
 * initialises the sempahor for mutual exclusion.
 *
 * Returns: statistics struct that is ready to be used by all client threads
 * and the SIGHUP handling thread
 */
Statistics* configure_stats() {
    Statistics* stats = malloc(sizeof(Statistics));

    //Zeroes out all the stats
    stats->numConnected = 0;
    stats->numCompleted = 0;
    stats->cracks = 0;
    stats->failedCracks = 0;
    stats->successCracks = 0;
    stats->crypts = 0;
    stats->cryptCalls = 0;

    //Creates the lock
    stats->lock = malloc(sizeof(sem_t));
    sem_init(stats->lock, 0, 1);
    return stats;
}

/* stats_thread()
 * --------------
 * Function that is ran by the SIGHUP handling thread. This function waits
 * until it gets a SIGHUP, then it prints out the server stats. It keeps doing
 * this until the server stops.
 *
 * v: void pointer to a StatsThreadData struct that contains a Statistics
 * struct and a set of signals
 */
void* stats_thread(void* v) {
    StatsThreadData* data = (StatsThreadData*)v;
    Statistics* stats = data->stats;
    int sig;

    while (1) {
        sigwait(data->set, &sig); //Wait until SIGHUP is received
        fprintf(stderr, STAT_MESSAGE, stats->numConnected, stats->numCompleted,
                stats->cracks, stats->failedCracks, stats->successCracks,
                stats->crypts, stats->cryptCalls);
        fflush(stderr);
    }
    return NULL;
}

/* process_connections()
 * ---------------------
 * This programs first sets up the Statistics struct by calling
 * configure_stats(), and then sets up the signal mask. It creates a thread
 * for stats and then sets up the semaphor to limit the number of concurrent
 * clients if this argument was specified on the command line. It then sits
 * in a loop waiting for clients to connect to the server.
 *
 * serv: listening socket
 * dict: Dictionary structure that contains word and the number of words in it
 * maxConns: the maximum number of concurrent clients allowed on the server
 */
void process_connections(int serv, Dictionary dict, int maxConns) {
    int fd;
    struct sockaddr_in fromAddr;
    socklen_t fromAddrSize;
    pthread_t threadID;

    Statistics* stats = configure_stats();

    sigset_t set;
    sigemptyset(&set); 
    sigaddset(&set, SIGHUP); //Add SIGHUP to signal set
    pthread_sigmask(SIG_BLOCK, &set, NULL); // mask SIGHUP for other threads

    StatsThreadData* statsThreadData = malloc(sizeof(StatsThreadData));
    statsThreadData->stats = stats;
    statsThreadData->set = &set;

    pthread_create(&threadID, 0, stats_thread, statsThreadData);
    pthread_detach(threadID); // Don't need stats thread return value
    
    sem_t maxConnsLock;
    if (maxConns == 0) {
        maxConns = SEM_VALUE_MAX;
    }
    sem_init(&maxConnsLock, 0, maxConns);

    // Repeatedly accept connections
    while (1) {
        fromAddrSize = sizeof(struct sockaddr_in);

        sem_wait(&maxConnsLock); // Handles max connections
        fd = accept(serv, (struct sockaddr*)&fromAddr, &fromAddrSize);

        if (fd < 0) {
            perror("Error accepting connection");
            exit(1);
        }
        stats_add_connection(stats); // Add 1 to connected stat

        ClientThreadData* data = malloc(sizeof(ClientThreadData));
        data->fd = fd;
        data->dict = &dict;
        data->maxConns = &maxConnsLock;
        data->stats = stats;

        pthread_create(&threadID, 0, client_wrapper, data);
        pthread_detach(threadID); // Don't need client thread return value
    }
}

/* client_wrapper()
 * ----------------
 * Wrapper function that takes in the void pointer for the client thread thread
 * and calls the client thread function with all of the arguments unwrapped.
 *
 * v: void pointer to a ClientThreadData struct containing the fd for the
 * client, a pointer to the dictionary, the semaphore to handle the maximum
 * number of connections and the statistics struct
 */
void* client_wrapper(void* v) {
    ClientThreadData* data = (ClientThreadData*)v;
    client_handler_thread(data->fd, data->dict, data->maxConns, data->stats);
    
    return NULL;
}

/* client_handler_thread()
 * -----------------------
 * Client handler thread function that reads each line from the server, updates
 * the total number of concurrent connections and the stats upon exit.
 *
 * fd: socket file descriptor
 * dict: Dictionary structure that contains word and the number of words in it
 * maxConns: the semaphor that handles the maximum number of concurrent clients
 * allowed to be on the server
 */
void client_handler_thread(int fd, Dictionary* dict, sem_t* maxConns,
        Statistics* stats) {
    char* line;
    int fd2 = dup(fd);
    FILE* in = fdopen(fd, "r");
    FILE* out = fdopen(fd2, "w");

    while ((line = read_line(in))) {
        process_command(line, out, dict, stats);
    }
    
    // Once done, allow another client connection and remove 1 from 
    // current connected clients stat
    stats_complete_connection(stats);

    fclose(in);
    fclose(out);
    close(fd2);
    close(fd);
    sem_post(maxConns);
}

/* process_command()
 * -----------------
 * Processes each command from the client, determining whether it is a crack,
 * crypt or invalid request. Whilst processing this command, it also updates
 * the Statistics struct. Once processed it sends a message back to the client.
 *
 * command: command sent by the client
 * out: file that is used for messages getting sent to the server
 * dict: Dictionary structure that contains word and the number of words in it
 * stats: Statistics struct that contains all of the server statistics
 */
void process_command(char* command, FILE* out, Dictionary* dict,
        Statistics* stats) {
    char** parts = split_by_char(command, ' ', MAX_FIELDS);
    char* result;

    if (parts[0] == NULL) {
        result = INVALID;
    } else if (strcmp(parts[0], "crack") == 0) {
        stats_add_crack_request(stats);
        // Check if ciphertext length valid, number of threads valid
        if (parts[1] == NULL || parts[2] == NULL) {
            result = INVALID;
        } else if (strlen(parts[1]) != CIPHER_LENGTH || 
                !valid_thread_num(parts[2])) {
            result = INVALID;
        // Checks if salt in substring is valid
        } else if (!(valid_salt_character(parts[1][0]) && 
                valid_salt_character(parts[1][1]))) {
            result = INVALID;
        } else {
            result = crack_call(parts[1], atoi(parts[2]), dict, stats);
        }
    } else if (strcmp(parts[0], "crypt") == 0) {
        stats_add_crypt_request(stats);
        // Checking salt
        if (parts[1] == NULL || parts[2] == NULL) {
            result = INVALID;
        } else if (!valid_salt(parts[2])) {
            result = INVALID;
        } else {
            result = crypt_call(parts[1], parts[2]);
            stats_add_crypt_call(stats, 1);
        }
    } else {
        result = INVALID;
    }

    fprintf(out, "%s\n", result);
    fflush(out);
}

/* create_crack_thread_data()
 * --------------------------
 * Creates the struct CrackThreadData that is sent to each cracking thread, and
 * then returns it.
 *
 * cipherText: cipher text that is being cracked
 * salt: salt part of the ciphertext (first 2 characters)
 * words: array of words from the dictionary
 * startPos: position of the dictionary that this thread will start cracking at
 * endPos: position of the dictionary that this thread will stop cracking at
 * found: flag telling each thread if they've found the word, so they can
 * stop useless cracking
 *
 * Returns: CrackThreadData struct
 */
CrackThreadData* create_crack_thread_data(char* cipherText, char* salt,
        char** words, int startPos, int endPos, volatile int* found) {
    CrackThreadData* data = malloc(sizeof(CrackThreadData));
    // Packages up crack thread data struct
    data->cipherText = cipherText;
    data->salt = salt;
    data->words = words;
    data->startPos = startPos;
    data->endPos = endPos;
    data->found = found;

    return data;
}

/* crack_call()
 * ------------
 * Function that coordinates the cracking of ciphertext. If a multi-threaded
 * crack is requested, this function splits the dictionary up into parts for
 * each thread to use. It then creates each cracking thread and then waits on
 * a result from these threads. Additionally, it also updates the Statistics
 * struct.
 * 
 * cipherText: cipher text that is being cracked
 * numThreads: number of threads that is requested to being used to crack this
 * cipher text
 * dict: Dictionary struct that contains the words and the number of words
 * stats: Statistics struct that contains all of the server statistics
 *
 * Returns: the result of the cracking. Either the actual text or a failed
 * string
 */
char* crack_call(char* cipherText, int numThreads, Dictionary* dict,
        Statistics* stats) {
    CrackThreadReturn* crackReturned;
    char* result = NULL;
    // Extract salt from cipher text
    char* salt = malloc(sizeof(char) * (SALT_LENGTH + 1));
    strncpy(salt, cipherText, SALT_LENGTH);

    // Dictionary start and end points
    int startPos = 0;
    int endPos;
    int increment = 0;

    //Calculate start and end points
    if (dict->numWords < numThreads || numThreads == 1) {
        endPos = dict->numWords;
        numThreads = 1; //Only one thread used
    } else {
        increment = dict->numWords / numThreads;
        endPos = increment;
    }
    volatile int* found = malloc(sizeof(int));
    *found = 0;

    pthread_t tids[numThreads]; // Store all of the thread ids
    // Create each thread
    for (int i = 0; i < numThreads; i++) {
        if ((numThreads - 1) == i) {
            endPos = dict->numWords;
        }
        CrackThreadData* data = create_crack_thread_data(cipherText, salt,
                dict->words, startPos, endPos, found); 
        pthread_create(&tids[i], 0, crack_thread, data);
        startPos += increment;
        endPos += increment;
    }
    // Wait on the result of each thread
    for (int i = 0; i < numThreads; i++) {
        pthread_join(tids[i], (void**) &crackReturned);
        stats_add_crypt_call(stats, crackReturned->numCalls);
        if (crackReturned->word != NULL) {
            stats_add_crack_request_pass(stats);
            result = crackReturned->word;
        }
    }
    if (result == NULL) {
        stats_add_crack_request_fail(stats);
        result = FAILED;
    }
    return result;
}

/* crack_thread()
 * --------------
 * Function that tries to brute-force crack some ciphertext. This function
 * will go through a subset of words in the dictionary (all words in the
 * dictionary if single-threaded crack) and encrypt each one. If it has found
 * a match, it will return this word and notify all of the other threads to
 * stop cracking.
 *
 * v: void pointer to CrackThreadData struct that contains the cipher text,
 * salt, words to go through, a start and an end point and the flag to tell
 * other threads to stop.
 *
 * Returns: CrackThreadReturn struct that contains the result of the crack
 * (either the word or null) and the number of crypt calls that the thread made
 */
void* crack_thread(void* v) {

    // Unwrapping done in the function
    CrackThreadData* crackData = (CrackThreadData*)v;

    CrackThreadReturn* crackReturned = malloc(sizeof(crackReturned));
    crackReturned->word = NULL;
    crackReturned->numCalls = 0;

    char* hash;

    struct crypt_data data;
    // Zero the entire data struct
    memset(&data, 0, sizeof(struct crypt_data));

    // Go through each word in the dictionary and brute-force
    for (int i = crackData->startPos; i < crackData->endPos &&
            *crackData->found == 0; i++) {
        hash = crypt_r(crackData->words[i], crackData->salt, &data);
        crackReturned->numCalls++; // Increase the number of crypt calls
        // If its a match, set the result, tell other threads to stop, return
        if (strcmp(hash, crackData->cipherText) == 0) {
            *crackData->found = 1;
            crackReturned->word = crackData->words[i];
            return (void*) crackReturned;
        }
    }
    // Return if nothing found or other thread found result
    return (void*) crackReturned;
}

/* crypt_call()
 * ------------
 * Creates and returns cipher text based on some crypt text and a salt.
 *
 * crypText: crypt text used to make cipher text
 * salt: salt used to make cipher text
 *
 * Returns: cipher text (hash)
 */
char* crypt_call(char* cryptText, char* salt) {
    char* hash;
    // Create crypt struct in order to use crypt_r (reentrant version)
    struct crypt_data data;
    // Zero the entire data struct
    memset(&data, 0, sizeof(struct crypt_data));
    hash = crypt_r(cryptText, salt, &data);
    return hash;
}

/* stats_add_connection()
 * ----------------------
 * Increments the total number of active connections by 1. Uses wait and post
 * to ensure only 1 thread is modifying the struct at a time.
 *
 * stats: Statistics struct that contains all of the server statistics
 */
void stats_add_connection(Statistics* stats) {
    sem_wait(stats->lock);
    stats->numConnected++;
    sem_post(stats->lock);
}

/* stats_complete_connection()
 * ---------------------------
 * Decrements the total number of active connctions by 1, and increases the 
 * total number of completed connections by 1. Uses wait and post to ensure
 * that only 1 thread is  modifying the struct at a time.
 *
 * stats: Statistics struct that contains all of the server statistics
 */
void stats_complete_connection(Statistics* stats) {
    sem_wait(stats->lock);
    stats->numConnected--;
    stats->numCompleted++;
    sem_post(stats->lock);
}

/* stats_add_crack_request()
 * -------------------------
 * Increments the total number of crack requests by 1. Uses wait and post to 
 * ensure that only 1 thread is modifying the struct at a time.
 *
 * stats: Statistics struct that contains all of the server statistics
 */
void stats_add_crack_request(Statistics* stats) {
    sem_wait(stats->lock);
    stats->cracks++;
    sem_post(stats->lock);
}

/* stats_add_crypt_request()
 * -------------------------
 * Increments the total number of crypt requests by 1. Uses wait and post to
 * ensure that only 1 thread is modifying the struct at a time.
 *
 * stats: Statistics struct that contains all of the server statistics
 */
void stats_add_crypt_request(Statistics* stats) {
    sem_wait(stats->lock);
    stats->crypts++;
    sem_post(stats->lock);
}

/* stats_add_crypt_request()
 * -------------------------
 * Adds the given number to the total number of crypt and crypt_r calls made
 * by the server. Uses wait and post to ensure that only 1 thread is modifying
 * the struct at a time.
 *
 * stats: Statistics struct that contains all of the server statistics
 * num: the number of crypt and crypt_r calls to be added to the stats
 */
void stats_add_crypt_call(Statistics* stats, int num) {
    sem_wait(stats->lock);
    stats->cryptCalls += num;
    sem_post(stats->lock);
}

/* stats_add_crack_request_pass()
 * ------------------------------
 * Increments the total number of passed crack requests by 1. Uses wait and 
 * post to ensure that only 1 thread is modifying the struct at a time.
 *
 * stats: Statistics struct that contains all of the server statistics
 */
void stats_add_crack_request_pass(Statistics* stats) {
    sem_wait(stats->lock);
    stats->successCracks++;
    sem_post(stats->lock);
}

/* stats_add_crack_request_fail()
 * ------------------------------
 * Increments the total number of failed crack requests by 1. Uses wait and
 * post to ensure that only 1 thread is modifying the struct at a time.
 *
 * stats: Statistics struct that contains all of the server statistics
 */
void stats_add_crack_request_fail(Statistics* stats) {
    sem_wait(stats->lock);
    stats->failedCracks++;
    sem_post(stats->lock);
}

/* valid_salt()
 * ------------
 * Determines whether a given salt is valid or not. This function checks the 
 * length of the salt (valid length is 2). It also checkers whether each
 * character in the salt is a valid salt character (alphabet,'.','/', 0-9)
 *
 * salt: salt that is to be validated
 *
 * Returns: whether the salt is valid or not
 */
bool valid_salt(char* salt) {
    // Check the length of the salt
    if (strlen(salt) != SALT_LENGTH) {
        return false;
    }

    // Check that each char in the salt is valid
    for (int i = 0; i < SALT_LENGTH; i++) {
        if (!valid_salt_character(salt[i])) {
            return false;
        }
    }
    return true;
}

// Checking if the salt is a valid character
// ASCII values are used to check if number or . or /
/* valid_salt_character()
 * ----------------------
 * Determiens whether or not a given salt character is valid or not. For a salt
 * character to be valid it must either be part of the alphabet, a number from 
 * 0 to 9, or a '.' or '/'
 *
 * salt: salt character that is being validated
 *
 * Returns: whether the salt character is valid or not
 */
bool valid_salt_character(char salt) {
    return isalpha(salt) || (((salt >= ASCII_MIN) && (salt <= ASCII_MAX)));
}

/* valid_thread_num()
 * ------------------
 * Determines whether a provided thread number for a crack request is valid or
 * not. For this number to be valid it must be greater or equal to 1 and less 
 * than or equal to 50.
 *
 * numThreads: the number of threads in a crack request (as a string)
 *
 * Returns: whether the number of threads is valid or not
 */
bool valid_thread_num(char* numThreads) {
    int num = atoi(numThreads); // Not using string_to_number as 0 is not valid
    if (num < MIN_THREADS || num > MAX_THREADS) {
        return false;
    }

    if (strlen(numThreads) > 2) {
        return false;
    }

    return true;
}

/* fill_dictionary()
 * -----------------
 * Creates a Dictionary struct containing all of the words contained in a given
 * dictionary file name. If this given file name is invalid, an error will be
 * thrown. Additionally, if this file doesn't contain any valid dictionary
 * words an error will be thrown.
 *
 * dictName: name of the dictionary chosen
 *
 * Returns: Dictionary struct containing the words and the number of words
 */
Dictionary fill_dictionary(char* dictName) {
    Dictionary param = {.words = NULL, .numWords = 0};
    param.words = malloc(0);
    char* line;
    
    FILE* dictFileStream = fopen(dictName, "r");

    if (!dictFileStream) {
        free(param.words);
        dictionary_error(dictName);
    }
    while ((line = read_line(dictFileStream))) {
        if (strlen(line) > MAX_WORD_LENGTH) {
            free(line);
            continue;
        }
        param.numWords++;
        param.words = realloc(param.words, param.numWords * sizeof(char*));
        param.words[param.numWords - 1] = line;
    }
    if (!param.numWords) {
        free(param.words);
        empty_dictionary_error();
    }


    return param;
} 

/* free_dictionary()
 * -----------------
 * Frees all of the memory allocated for a dictionary struct.
 * 
 * dict: Dictionary struct to be freed
 */
void free_dictionary(Dictionary dict) {
    for (int i = 0; i < dict.numWords; i++) {
        free(dict.words[i]);
    }
    free(dict.words);
}

/* string_to_number()
 * ------------------
 * Converts a string to a number. If this string is not a valid number, a usage
 * error will be thrown. This function is only used when checking command line
 * so the usage error thrown is acceptable.
 *
 * arg: the number to convert
 *
 * Returns: the converted number
 */
int string_to_number(char* arg) {
    char* end;
    int num;
    num = strtol(arg, &end, 10);

    // Checking if entire string was read and converted
    if (*end != '\0') {
        usage_error();
    } 
    return num;
}

/* validate_port_num()
 * -------------------
 * Determines whether or not a given port number is valid or not. To be valid
 * the port number must be less than the max port number and greater than the
 * minimum. If the port number is invalid, a usage error is thrown.
 *
 * portNum: port number
 */
void validate_port_number(int portNum) {
    if (portNum != 0 && (portNum < MIN_PORT || portNum > MAX_PORT)) {
        usage_error();
    }
}

/* validate_max_connections()
 * --------------------------
 * Validates whether or not the the maximum number of connections provided is
 * valid or not. Just checks wheteher the number is above 0. If the number is
 * invalid, a usage error will be thrown.
 *
 * maxConns: number to validate
 *
 * Returns: the number of connections
 */
int validate_max_connections(int maxConns) {
    // Checking if negative number given
    if (maxConns < 0) {
        usage_error();
    }
    return maxConns;
}

/* usage_error()
 * -------------
 * Prints the usage error to stderr and exits with the appropriate status.
 */
void usage_error() {
    fprintf(stderr, "Usage: crackserver [--maxconn connections] [--port "\
            "portnum] [--dictionary filename]\n");
    exit(USAGE_ERROR);
}

/* dictionary_error()
 * -------------
 * Prints the dictionary error if it was unable to be opened to stderr and
 * exits with the appropriate status.
 */
void dictionary_error(char* dictName) {
    fprintf(stderr, "crackserver: unable to open dictionary file \"%s\"\n", 
            dictName);
    exit(DICT_FILE_ERROR);
}

/* empty_dictionary_error()
 * -------------
 * Prints an error to stderr saying that the dictionary file has no valid words
 * in it, and exits with the appropriate status.
 */
void empty_dictionary_error() {
    fprintf(stderr, "crackserver: no plain text words to test\n");
    exit(NO_WORDS_ERROR);
}

/* unable_listen_erro()
 * -------------
 * Prints a message to stderr if a socket is unabled to be opened for
 * listening, and exits with the appropriate status.
 */
void unable_listen_error() {
    fprintf(stderr, "crackserver: unable to open socket for listening\n");
    exit(UNABLE_OPEN_ERROR);
}
