#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <csse2310a4.h>
#include <csse2310a3.h>

#define MAX_ARGS 2
#define MIN_ARGS 1

#define BUFFER_SIZE 80

// Error messages
#define USAGE_MESSAGE "Usage: crackclient portnum [jobfile]\n"
#define JOB_FILE_MESSAGE "crackclient: unable to open job file \"%s\"\n"
#define CONNECTION_ERROR_MESSAGE "crackclient: unable to connect to port %s\n"
#define TERMINATE_MESSAGE "crackclient: server connection terminated\n"

// Server responses
#define SERVER_INVALID ":invalid\n"
#define INVALID_MESSAGE "Error in command\n"
#define SERVER_FAILED ":failed\n"
#define FAILED_MESSAGE "Unable to decrypt\n"

// Enum to hold exit statuses
typedef enum {
    USAGE_ERROR = 1,
    JOB_FILE_ERROR = 2,
    CONNECTION_ERROR = 3,
    CONNECTION_TERMINATED = 4,
    OK = 0
} ExitStatus;

// Struct that contains information about the client - port number and jobfile
typedef struct {
    const char* portNum;
    char* jobFile;
} ClientDetails;

// Function prototypes
ClientDetails parse_command_line(int argc, char** argv);
int setup_connection(const char* port);
void communicate_with_server(int connFD, char* jobFile);
void send_command(char* line, FILE* out);
void handle_response(char* response);

int main(int argc, char** argv) {
    ClientDetails clientDetails;
    int connFD;
    
    // Check the command line and get info from it
    clientDetails = parse_command_line(argc, argv);
    // Setup connection with the server
    connFD = setup_connection(clientDetails.portNum);

    // Check if connection is valid or not
    if (connFD < 0) {
        fprintf(stderr, CONNECTION_ERROR_MESSAGE, clientDetails.portNum);
        exit(CONNECTION_ERROR);
    }

    // Start communicating with the server
    communicate_with_server(connFD, clientDetails.jobFile);

    return OK;
}

/* communicate_with_server()
 * -------------------------
 * Sends requests to server and gets responses back from server. Depending on
 * whether the jobfile was specified on the command line, these requests are
 * either from the file or from stdin. Also checks as to whether the connection
 * with the server has been terminated.
 *
 * connFD: file descriptor for communicating with the server
 * jobFile: name of the job file with commands to be sent to the server
 */
void communicate_with_server(int connFD, char* jobFile) {
    FILE* inputSource;
    char* line;
    char buffer[BUFFER_SIZE];
    bool terminated = false;

    //Setting up to and from connectors from client<->server
    int fd2 = dup(connFD);
    FILE* out = fdopen(connFD, "w");
    FILE* in = fdopen(fd2, "r");

    if (jobFile) {
        inputSource = fopen(jobFile, "r");
    } else {
        inputSource = stdin;
    }

    //Read from stdin or jobFile
    while ((line = read_line(inputSource))) {
        // Check for blank lines and comments
        if (line[0] == '#' || line[0] == '\0') {
            free(line);
            continue;
        }
        // Send line to server
        send_command(line, out);

        // Check if connection was terminated
        if (!fgets(buffer, sizeof(buffer) - 1, in)) {
            terminated = true;
            break;
        }

        // Interpret server reponse
        handle_response(buffer);
    }
    fclose(inputSource);
    fclose(in);
    fclose(out);
    close(fd2);
    close(connFD);

    if (terminated) {
        fprintf(stderr, TERMINATE_MESSAGE);
        exit(CONNECTION_TERMINATED);
    } 

}

/* send_command()
 * --------------
 * Sends a given command to the server.
 *
 * line: command to send to the server
 * out: file that is used as the output of the client to the server
 */
void send_command(char* line, FILE* out) {
    fprintf(out, "%s\n", line);
    fflush(out);
    free(line);
}

/* handle_response()
 * -----------------
 * Handles the response back from the server. If failed or invalid messages are
 * sent from the server, these are handled and displayed. If else, the message
 * is just displayed.
 *
 * response: response from the server
 */
void handle_response(char* response) {
    if (!strcmp(response, SERVER_INVALID)) {
        printf("%s", INVALID_MESSAGE);
    } else if (!strcmp(response, SERVER_FAILED)) {
        printf("%s", FAILED_MESSAGE);
    } else {
        printf("%s", response);
    }
    fflush(stdout);
}

/* setup_connection()
 * ------------------
 * Sets up a connection with a server listening on a given port.
 *
 * port: port that the server is listening on.
 *
 * Returns: file descriptor for communicating with the server
 */
int setup_connection(const char* port) {
    struct addrinfo* ai = 0;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int err;

    // Checking if the address could be worked out
    if ((err = getaddrinfo("localhost", port, &hints, &ai))) {
        freeaddrinfo(ai);
        return -1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(fd, ai->ai_addr, sizeof(struct sockaddr))) {
        return -1;
    }
    freeaddrinfo(ai);
    return fd;

}

/* parse_command_line()
 * --------------------
 * Checks the command line arguments to ensure that they are correct and that
 * at least a port number has been provided. Also checks whether the job file
 * provided is accurate.
 *
 * argc: number of arguments passed to the program
 * argv: arguments passed to the program
 *
 * Returns: ClientDetails struct that contains the port number and the job file
 * for the client
 */
ClientDetails parse_command_line(int argc, char** argv) {
    ClientDetails clientDetails = {.portNum = NULL, .jobFile = NULL};

    // Skip program name
    argc--;
    argv++;

    // Checks whether the correct number of arguments was provided
    if (argc > MAX_ARGS || argc < MIN_ARGS) {
        fprintf(stderr, USAGE_MESSAGE);
        exit(USAGE_ERROR);
    }

    clientDetails.portNum = argv[0];

    // Checks if jobfile was provided
    if (argc == 2) {
        clientDetails.jobFile = argv[1];
        FILE* jobFile = fopen(argv[1], "r");
        // Checks if job file is valid
        if (!jobFile) {
            fprintf(stderr, JOB_FILE_MESSAGE, clientDetails.jobFile);
            exit(JOB_FILE_ERROR);
        }
    }

    return clientDetails;
}
