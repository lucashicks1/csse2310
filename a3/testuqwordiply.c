#include <csse2310a3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>

// Name of program that is used as solution to uqwordiply
#define UQ_SOLUTION "demo-uqwordiply"

// Limits on number of arguments
#define MIN_NUM_ARGS 2
#define MAX_NUM_ARGS 4

//MACROS FOR ARRAY INDEXING FOR PROCESSES
#define TEST_PROG 0
#define DEMO_PROG 1
#define UQCMP_STDOUT 2
#define UQCMP_STDERR 3

// MACROS FOR ARRAY INDEXING JOB NUMBERS
#define NUM_JOBS_PASSED 0
#define NUM_JOBS_RUN 1

// Pipes are labelled as per figure 1 in spec sheet
#define PIPE_ONE 0
#define PIPE_TWO 1
#define PIPE_THREE 2
#define PIPE_FOUR 3

// Ends of the pipes to make easier to read
#define READ_END 0
#define WRITE_END 1

// Number of pipes used in the program as per figure 1 in spec sheet 
#define NUM_PIPES_JOB 4
#define NUM_PIPE_ENDS 2

// Number of child processes created for each job 
#define NUM_PROCESSES_JOB 4

// Numbers added to the successful job counter after test is ran
#define JOB_SUCCESS 1
#define JOB_FAIL 0

// Job statements that are printed to stdout and stderr
#define JOB_DESCRIPTION "Job %d %s"
#define UNABLE_EXECUTE "Job %d: Unable to execute test\n"
#define STDOUT_DIFFERS "Job %d: Stdout differs\n"
#define STDOUT_MATCHES "Job %d: Stdout matches\n"
#define STDERR_DIFFERS "Job %d: Stderr differs\n"
#define STDERR_MATCHES "Job %d: Stderr matches\n"
#define EXIT_MATCHES "Job %d: Exit status matches\n"
#define EXIT_DIFFERS "Job %d: Exit status differs\n"
#define OVERALL_RESULT "testuqwordiply: %d out of %d tests passed\n"
#define START_JOB_MESSAGE "Starting job %d\n"
#define START_JOB_MESSAGE "Starting job %d\n"

// Enum to hold exit statuses
typedef enum {
    OK = 0,
    TESTS_FAILED = 1,
    USAGE_ERROR = 2,
    JOB_FILE_ERROR = 3,
    JOB_FORMAT_ERROR = 4,
    INPUT_FILE_ERROR = 5,
    BLANK_JOB_ERROR = 6,
    EXEC_FAILED = 99
} ExitStatus;

// Structure to hold program information including optional arguments and the
// names of the test program file and job file
typedef struct {
    bool quiet;
    bool parallel;
    char* testProgramName;
    char* jobFileName;
} ProgramParameters;

// Structure to hold information for the jobs found in the jobfile
typedef struct {
    int totalNumJobs;
    char*** jobs;
    char** inputFiles;
} JobDetails;

// Function prototypes - program related
ProgramParameters parse_args(int argc, char** argv);
JobDetails parse_job_file(char* jobFileName);
char** create_job(char* argLine);
void check_input_file(char* inputFileName, char* argLine, char*** jobs, 
        char** inputFiles, int totalNumJobs, int lineNum, char* jobFileName,
        FILE* file);
int* run_test_jobs(ProgramParameters programParams, JobDetails jobDetails);
int* start_job(char* testFileName, char* inputFileName, char** args,
        int jobNum, bool quiet);
int run_test_program(char* testFileName, char* inputFileName, char** args,
        int pipeFDs[NUM_PIPES_JOB][2]);
int run_demo_program(char* inputFileName, char** args,
        int pipeFDs[NUM_PIPES_JOB][2]);
int run_uqcmp(char* checkDest, int jobNum, bool quiet,
        int pipeFDs[NUM_PIPES_JOB][2]);
int report_job(int jobNum, int* processIDs);
void free_statuses(int** jobProcessIDs, int numJobs);
void send_sigkill(int processIDs[NUM_PROCESSES_JOB]);
void quiet_redirect();
void interrupt_test();
void job_sleep();
void free_job_details(char*** jobs, char** inputFiles, int totalNumJobs);

// Function prototypes - helper functions
int count_letter(char* word, char letter);
int count_digits(int num);

// Function prototypes - error functions
void usage_error();
void job_file_error(char* fileName);
void input_file_error(char* inputFileName, int lineNum, char* fileName);
void job_format_error(int lineNum, char* fileName);
void blank_job_error(char* fileName);

// Global boolean for signal handler
volatile bool jobsInterrupted = false;

int main(int argc, char** argv) {
    // Duplicate fd 3 and 4, and then close fds 3 and 4 pointing to the 
    // duplicated file description
    dup(3);
    dup(4);
    close(3);
    close(4);

    ProgramParameters programParams;
    JobDetails jobDetails;

    // Configures the signal handler for SIGINT to terminate jobs
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = interrupt_test;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, 0);

    // Parses the command line arguments
    programParams = parse_args(argc, argv);

    // Parses the job file provided
    jobDetails = parse_job_file(programParams.jobFileName);

    // Runs all of the test jobs and returns the number of jobs run and the
    // number of jobs that passed
    int* results = run_test_jobs(programParams, jobDetails);
    printf(OVERALL_RESULT, results[NUM_JOBS_PASSED], results[NUM_JOBS_RUN]);

    // Frees memory and exits the program with the appropriate status
    // depending on whether all jobs passed or not
    
    free_job_details(jobDetails.jobs, jobDetails.inputFiles, 
            jobDetails.totalNumJobs);

    if (results[NUM_JOBS_PASSED] == results[NUM_JOBS_RUN]) {
        free(results);
        return OK;
    } else {
        free(results);
        return TESTS_FAILED;
    }
}

/* parse_args()
 * ------------
 * Interprets any command line arguments given and parses this information into
 * a ProgramParameters struct. Exits the program and prints error messages if
 * an incorrect command line is given.
 *
 * argc: number of arguments passed to the program
 * argv: arguments passed to the program
 *
 * Returns: ProgramParameters struct containing program information
 */
ProgramParameters parse_args(int argc, char** argv) {
    ProgramParameters info = { .quiet= false, .parallel = false,
            .testProgramName = NULL, .jobFileName = NULL};
    // Skip over program name argument
    argc--;
    argv++;
    // Check amount of arguments
    if (argc < MIN_NUM_ARGS || argc > MAX_NUM_ARGS) {
        usage_error();
    }
    // Check for optional arguments if they exist
    while (argc > MIN_NUM_ARGS) {
        if (!strcmp(argv[0], "--quiet") && !info.quiet) {
            info.quiet= true;
        } else if (!strcmp(argv[0], "--parallel") && !info.parallel) {
            info.parallel = true;
        } else {
            usage_error();
        }
        argc--;
        argv++;
    }
    // Checks if --quiet or --parallel is used after arguments
    if ((argv[0][0] == '-' && argv[0][1] == '-') || 
            (argv[1][0] == '-' && argv[1][1] == '-')) {
        usage_error();
    }
    info.testProgramName = argv[0];
    info.jobFileName = argv[1];

    return info;
}

/* parse_job_file()
 * ----------------
 * Parses the job file that is provided. Checks whether each line in the job
 * file is syntactically valid.
 *
 * jobFileName: name of the job file initially specified on the command line
 *
 * Returns: JobDetails struct containing information on jobs
 */
JobDetails parse_job_file(char* jobFileName) {
    FILE* file = fopen(jobFileName, "r");
    if (!file) {
        job_file_error(jobFileName);
    }
    int totalNumJobs = 0;
    char* line;
    int lineNum = 0;
    char*** jobs = malloc(0);
    char** inputFiles = malloc(0);
    while ((line = read_line(file))) {
        lineNum++;
        if (line[0] == '#' || line[0] == '\0') { // Check comments, blank lines
            free(line);
            continue;
        }
        if (line[0] == ',' || count_letter(line, ',') != 1) { // Check commas
            fclose(file);
            free(line);
            free_job_details(jobs, inputFiles, totalNumJobs);
            job_format_error(lineNum, jobFileName);
        }
        char** jobItems = split_line(line, ',');
        char* inputFileName = strdup(jobItems[0]);
        char* argLine = strdup(jobItems[1]);
        free(jobItems[0]);
        free(jobItems);
        check_input_file(inputFileName, argLine, jobs, inputFiles,
                totalNumJobs, lineNum, jobFileName, file);
        char** job = create_job(argLine);
        jobs = realloc(jobs, (totalNumJobs + 1) * sizeof(char**));
        inputFiles = realloc(inputFiles, (totalNumJobs + 1) * sizeof(char*));
        inputFiles[totalNumJobs] = inputFileName;
        jobs[totalNumJobs] = job;
        totalNumJobs++;
        free(argLine);
    }
    free(line);
    fclose(file);
    if (!totalNumJobs) {
        free_job_details(jobs, inputFiles, totalNumJobs);
        blank_job_error(jobFileName);
    }
    JobDetails jobDetails = {.totalNumJobs = totalNumJobs, .jobs = jobs, 
            .inputFiles = inputFiles};
    return jobDetails;
}

/* check_input_file()
 * -------------------
 * Checks whether the input file name specified in the job file points to a 
 * valid input file. If not, it calls other functions to print error messages, 
 * free any allocated memory and exits the function appropriately.
 *
 * inputFileName: name of the input file on the job line
 * argLine: line of arguments read from job file
 * jobs: 3D array containing arguments for each job
 * inputFiles: 2D array containing the input file for each job
 * totalNumJobs: the total number of jobs
 * lineNum: line number that the job was on
 * jobFileName: the name of the job file
 * file: FILE* object for the job file
 *
 */
void check_input_file(char* inputFileName, char* argLine, char*** jobs,
        char** inputFiles, int totalNumJobs, int lineNum, char* jobFileName, 
        FILE* file) {
    FILE* inputFile = fopen(inputFileName, "r");
    if (!inputFile) {
        free(argLine);
        fclose(file);
        free_job_details(jobs, inputFiles, totalNumJobs);
        input_file_error(inputFileName, lineNum, jobFileName);
    }
    fclose(inputFile);
}

/* create_job()
 * ------------
 * Takes a list of arguments given on a line in the job file and creates a 
 * 2D character array that contains the arguments for each job and makes 
 * space for the program name aswell.
 *
 * argLine: line of arguments read from job file
 *
 * Returns: 2D character array containing arguments for each job
 */
char** create_job(char* argLine) {
    int numArgs;
    char** args = split_space_not_quote(argLine, &numArgs);
    char** job = malloc((numArgs + 2) * sizeof(char*));
    memset(job, 0, (numArgs + 2) * sizeof(char*));

    job[0] = NULL; // To be replaced by program name when execed
    job[numArgs + 1] = NULL;

    // Move arguments one position to the right in the array
    if (numArgs) {
        for (int i = 0; i < numArgs; i++) {
            job[i + 1] = strdup(args[i]);
        }
    }
    free(args);
    return job;
}

/* run_test_jobs()
 * ---------------
 * Runs all of the test jobs that were specified in the job file provided. The
 * way that it runs these jobs is dependent on whether the --parallel argument
 * was given on the command line.
 *
 * programParams: ProgramParameters struct containing program information
 * jobDetails: JobDetails struct containing information on each job
 *
 * Returns: int* with the total number of jobs ran and jobs passed
 */
int* run_test_jobs(ProgramParameters programParams, JobDetails jobDetails) {
    int* results = calloc(2, sizeof(int));
    if (programParams.parallel) {
        int** jobProcessIDs = malloc(sizeof(int*) * jobDetails.totalNumJobs);
        for (int i = 0; i < jobDetails.totalNumJobs && !jobsInterrupted; i++) {
            printf(START_JOB_MESSAGE, i + 1);
            fflush(stdout);
            //Start each job
            jobProcessIDs[i] = start_job(programParams.testProgramName,
                   jobDetails.inputFiles[i], jobDetails.jobs[i], i + 1,
                   programParams.quiet);
            results[NUM_JOBS_RUN] += 1;
        }
        job_sleep();
        for (int i = 0; i < results[NUM_JOBS_RUN]; i++) {
            send_sigkill(jobProcessIDs[i]);
        }
        for (int i = 0; i < results[NUM_JOBS_RUN]; i++) {
            results[NUM_JOBS_PASSED] += report_job(i + 1, jobProcessIDs[i]);
        }
        free_statuses(jobProcessIDs, results[NUM_JOBS_RUN]);
    } else {
        int* processIDs;
        for (int i = 0; i < jobDetails.totalNumJobs && !jobsInterrupted; i++) {
            // Start the job
            printf(START_JOB_MESSAGE, i + 1);
            fflush(stdout);
            processIDs = start_job(programParams.testProgramName,
                    jobDetails.inputFiles[i], jobDetails.jobs[i], i + 1,
                    programParams.quiet);
            results[NUM_JOBS_RUN] += 1;
            job_sleep();
            send_sigkill(processIDs);
            results[NUM_JOBS_PASSED] += report_job(i + 1, processIDs);
            free(processIDs);
        }
    }
    return results;
}

/* start_job()
 * -----------
 * Begins the process of starting a job. This function creates 4 child
 * processes, the test program, the demo solution, and two instances of uqcmp
 * that compare both stderrs and stdouts of both programs. Also sets the input
 * of both the test program and demo solution to the input file provided in 
 * the job file.
 *
 * testFileName: name of the test program to run
 * inputFileName: name of the input file that is given to the test and demo
 * programs
 * args; arguments specified for each job which is given to the test and demo
 * programs
 *
 * Returns: a int* containing the processIDs for the processes that make up a
 * job
 */
int* start_job(char* testFileName, char* inputFileName, char** args,
        int jobNum, bool quiet) {
    // Used to store 4 process IDs
    int* jobProcessIDs = malloc(sizeof(int) * NUM_PROCESSES_JOB); 

    // Creates fds for pipes numbered 1-4 as per diagram on spec sheet
    int pipeFDs[NUM_PIPES_JOB][2];

    // Creates pipes with file descriptors
    pipe(pipeFDs[PIPE_ONE]); //Uses fds 3 and 4 as first call to pipe
    pipe(pipeFDs[PIPE_TWO]);
    pipe(pipeFDs[PIPE_THREE]);
    pipe(pipeFDs[PIPE_FOUR]);

    //Create process A (test program)
    jobProcessIDs[TEST_PROG] = run_test_program(testFileName, inputFileName,
            args, pipeFDs);
    
    //Create process B (demo-uqwordiply)
    jobProcessIDs[DEMO_PROG] = run_demo_program(inputFileName, args, pipeFDs);
    
    //Create process C (uqcmp)
    jobProcessIDs[UQCMP_STDOUT] = run_uqcmp("stdout", jobNum,quiet, pipeFDs);

    //Create process D (uqcmp)
    jobProcessIDs[UQCMP_STDERR] = run_uqcmp("stderr", jobNum,quiet, pipeFDs);

    //Close read and write pipe ends in parent as not needed
    for (int i = 0; i < NUM_PIPES_JOB; i++) {
        close(pipeFDs[i][READ_END]);
        close(pipeFDs[i][WRITE_END]);
    }

    return jobProcessIDs;
}

/* run_test_program()
 * ------------------
 * Creates the child process for the test program and does pipe redirection.
 *
 * testFileName: name of the test program to run
 * inputFileName: name of the input file that is given to the test program
 * args: arguments that are run with the test program
 * pipeFDs: file descriptors for the four pipes used in the program
 *
 * Returns: the process ID of the child process for the test program
 */
int run_test_program(char* testFileName, char* inputFileName, char** args,
        int pipeFDs[NUM_PIPES_JOB][2]) {
    
    pid_t pID = fork();
    if (!pID) {
        // Child process
        //Close read ends of pipes 1 & 2 as only writing to those pipes
        close(pipeFDs[PIPE_ONE][READ_END]);
        close(pipeFDs[PIPE_TWO][READ_END]);
        // Close read and write ends of pipes 3 & 4 as not needed
        close(pipeFDs[PIPE_THREE][READ_END]);
        close(pipeFDs[PIPE_THREE][WRITE_END]);
        close(pipeFDs[PIPE_FOUR][READ_END]);
        close(pipeFDs[PIPE_FOUR][WRITE_END]);
        // Redirect pipe write ends to stdout and stderr
        dup2(pipeFDs[PIPE_ONE][WRITE_END], STDOUT_FILENO);
        close(pipeFDs[PIPE_ONE][WRITE_END]);
        dup2(pipeFDs[PIPE_TWO][WRITE_END], STDERR_FILENO);
        close(pipeFDs[PIPE_TWO][WRITE_END]);
        //Get stdin from file
        int fdInput = open(inputFileName, O_RDONLY);
        dup2(fdInput, STDIN_FILENO);
        close(fdInput);
        //Run test_program
        args[0] = testFileName;
        execvp(testFileName, args);
        perror("Error: ");
        exit(EXEC_FAILED);
    }
    return pID;
}

/* run_demo_program()
 * ------------------
 * Creates the child process for the demo program and does pipe redirection.
 *
 * inputFileName: name of the input file that is given to the demo program 
 * args: arguments that are run with the demo program
 * pipeFDs: file descriptors for the four pipes used in the program
 *
 * Returns: the process ID of the child process for the demo program
 */
int run_demo_program(char* inputFileName, char** args,
        int pipeFDs[NUM_PIPES_JOB][2]) {
        
    pid_t pID = fork();
    if (!pID) {
        // Child process
        //Close read ends of pipes 3 & 4 as only writing to those pipes
        close(pipeFDs[PIPE_THREE][READ_END]);
        close(pipeFDs[PIPE_FOUR][READ_END]);
        // Close read and write ends of pipes 1 & 2 as not needed
        close(pipeFDs[PIPE_ONE][READ_END]);
        close(pipeFDs[PIPE_ONE][WRITE_END]);
        close(pipeFDs[PIPE_TWO][READ_END]);
        close(pipeFDs[PIPE_TWO][WRITE_END]);
        // Redirect pipe write ends to stdout and stderr
        dup2(pipeFDs[PIPE_THREE][WRITE_END], STDOUT_FILENO);
        close(pipeFDs[PIPE_THREE][WRITE_END]);
        dup2(pipeFDs[PIPE_FOUR][WRITE_END], STDERR_FILENO);
        close(pipeFDs[PIPE_FOUR][WRITE_END]);
        //Get stdin from file
        int fdInput = open(inputFileName, O_RDONLY);
        dup2(fdInput, STDIN_FILENO);
        close(fdInput);
        //Run test program
        args[0] = UQ_SOLUTION;
        execvp(UQ_SOLUTION, args); // searching on path - use execvp not execv
        perror("Error: ");
        exit(EXEC_FAILED);
    }
    return pID;
}

/* run_uqcmp()
 * Creates a child process for uqcmp that compares either the stdout or the 
 * stderr of both the test program and demo program. It also handles the output
 * when the --quiet arguments is given on the command line. The same function
 * is used to create processes for stdout and stderr, but the redirection is
 * different.
 *
 * checkDest: either "stdout" or "stderr" depending on what the process is
 * checking
 * jobNum: the job number for the job that is currently being run
 * quiet: true if the uqcmp output is being suppressed. This is given on
 * command line.
 * pipeFDs: file descriptors for the four pipes used in the program
 *
 * Returns: processID for the uqcmp instance for that job
 */
int run_uqcmp(char* checkDest, int jobNum, bool quiet,
        int pipeFDs[NUM_PIPES_JOB][2]) {
    pid_t pID = fork();
    if (!pID) {
        //Child process
        //Close write ends of all pipes 
        //Pipe 1 ends aren't closed as they are fds 3 and 4
        close(pipeFDs[PIPE_TWO][WRITE_END]);
        close(pipeFDs[PIPE_THREE][WRITE_END]);
        close(pipeFDs[PIPE_FOUR][WRITE_END]);
        close(STDIN_FILENO);

        if (!strcmp(checkDest, "stdout")) {
            //If process is comparing stdout
            // Close read ends of pipes 2 & 4 as not needed
            close(pipeFDs[PIPE_TWO][READ_END]);
            close(pipeFDs[PIPE_FOUR][READ_END]);
            // Redirect to file descriptors 3 and 4
            dup2(pipeFDs[PIPE_ONE][READ_END], 3);
            dup2(pipeFDs[PIPE_THREE][READ_END], 4);
            close(pipeFDs[PIPE_THREE][READ_END]);
        } else {
            // Close read ends of pipe 3 as not needed
            close(pipeFDs[PIPE_THREE][READ_END]);
            // Redirect to file descriptors 3 and 4
            dup2(pipeFDs[PIPE_TWO][READ_END], 3);
            close(pipeFDs[PIPE_TWO][READ_END]);
            dup2(pipeFDs[PIPE_FOUR][READ_END], 4);
            close(pipeFDs[PIPE_FOUR][READ_END]);
        }
        int jobNumDigits = count_digits(jobNum);
        // 11 characters in job description 
        char jobDesc[11 + jobNumDigits];
        sprintf(jobDesc, JOB_DESCRIPTION, jobNum, checkDest);
        char* args[3] = {"uqcmp", jobDesc, NULL};
        if (quiet) {
            quiet_redirect();
        } 
        execvp(args[0], args);
        perror("Error: ");
        exit(EXEC_FAILED);
    }
    return pID;
}

/* quiet_redirect()
 * ----------------
 * Suppresses the output (stderr and stdout) of uqcmp processes.
 */
void quiet_redirect() {
    int devNull = open("/dev/null", O_WRONLY);
    dup2(devNull, STDOUT_FILENO);
    dup2(devNull, STDERR_FILENO);
    close(devNull);
}

/* job_sleep()
 * -----------
 * Suspends the execution of the program for a given time. Also handles whether
 * the sleep was interrupted by a signal, and continues any remaining sleep.
 */
void job_sleep() {
    struct timespec required, remaining;
    required.tv_sec = 2;
    required.tv_nsec = 0;
    while (nanosleep(&required, &remaining)) {
        required = remaining;

    }
}

/* report_job()
 * -------------
 * Reports on the outcome of a job and its 4 associated processes. Determines
 * whether all of the programs managed to execute sucessfully and compares the 
 * output of the uqcmp processes to determine whether the job passed.
 *
 * jobNum: the job number for the job that is currently being run
 * processIDs: int array containing the process IDs for a job
 *
 * Returns: 1 if the job passed, 0 if the job failed
 */
int report_job(int jobNum, int* processIDs) {
    bool testFailed = false;
    int* statuses = malloc(sizeof(int) * NUM_PROCESSES_JOB);
    for (int i = 0; i < NUM_PROCESSES_JOB; i++) {
        waitpid(processIDs[i], &statuses[i], WCONTINUED);
        if (WIFEXITED(statuses[i])) {
            statuses[i] = WEXITSTATUS(statuses[i]);
        } else {
            statuses[i] = WTERMSIG(statuses[i]);
        }
        if (statuses[i] == EXEC_FAILED) {
            free(statuses);
            printf(UNABLE_EXECUTE, jobNum);
            fflush(stdout);
            return JOB_FAIL;
        }
    }
    if (!statuses[2]) {
        printf(STDOUT_MATCHES, jobNum);
    } else {
        printf(STDOUT_DIFFERS, jobNum);
        testFailed = true;
    }
    fflush(stdout);

    if (!statuses[3]) {
        printf(STDERR_MATCHES, jobNum);
    } else {
        printf(STDERR_DIFFERS, jobNum);
        testFailed = true;
    }
    fflush(stdout);

    if (statuses[0] == statuses[1]) {
        printf(EXIT_MATCHES, jobNum);
    } else {
        printf(EXIT_DIFFERS, jobNum);
        testFailed = true;
    }
    fflush(stdout);
    
    free(statuses);
    if (testFailed) {
        return JOB_FAIL;
    }
    return JOB_SUCCESS;
}

/* interrupt_test()
 * ----------------
 * Basic signal handler that sets the signal global variable to true
 */
void interrupt_test() {
    jobsInterrupted = true;
}

/* free_statuses()
 * ---------------
 * Frees the memory allocated to store process IDs when jobs are run in
 * parallel mode.
 *
 * jobProcessIDs: 2D integer array containing all processIDs
 * numJobs: number of jobs run
 */
void free_statuses(int** jobProcessIDs, int numJobs) {
    for (int i = 0; i < numJobs; i++) {
        free(jobProcessIDs[i]);
    }
    free(jobProcessIDs);
}

/* free_job_details()
 * ------------------
 * Frees the memory allocated for storing the details inside the JobDetails
 * struct that come from the job file.
 *
 * jobs: 3D char array that holds the arguments for each job
 * inputFiles: 2D char array that holds the input file Name for each job
 * totalNumJobs: the total number of jobs
 */
void free_job_details(char*** jobs, char** inputFiles, int totalNumJobs) {
    for (int i = 0; i < totalNumJobs; i++) {
        free(inputFiles[i]);
        free(jobs[i][0]);
        for (int j =  1; jobs[i][j] != NULL; j++) {
            free(jobs[i][j]);
        }
        free(jobs[i]);
    }
    free(inputFiles);
    free(jobs);
}

/* send_sigkill()
 * --------------
 * Sends sigkill to each of the four processes that make up a job.
 *
 * processIDs: integer array containing the process IDs for the 4 processes 
 * that make up a job.
 */
void send_sigkill(int processIDs[NUM_PROCESSES_JOB]) {
    for (int i = 0; i < NUM_PROCESSES_JOB; i++) {
        kill(processIDs[i], SIGKILL);
    }

}

/* count_digits()
 * --------------
 * Counts the number of digits in an integer
 *
 * num: number of which its digits are counted
 *
 * Returns: the number of digits in the num
 */
int count_digits(int num) {
    int numCopy = num;
    int numDigits = 0;
    while (numCopy != 0) {
        numCopy /= 10;
        numDigits++;
    }
    return numDigits;
}

/* count_letter()
 * -----------------
 * Counts the number of occurrences of a character in a word.
 *
 * word: word to search for the character in
 * letter: the letter to search for
 *
 * Returns: number of occurrences of the letter in the word
 */
int count_letter(char* word, char letter) {
    int numOccurrences = 0;
    for (int i = 0; word[i] != '\0'; i++) {
        if (word[i] == letter) {
            numOccurrences++;
        }
    }
    return numOccurrences;
}

/* usage_error()
 * -------------
 * Prints the usage error to stderr and exits with the appropriate status.
 */
void usage_error() {
    fprintf(stderr, "Usage: testuqwordiply [--quiet] [--parallel] testprogram "
            "jobfile\n");
    exit(USAGE_ERROR);
}

/* job_file_error()
 * ----------------
 * Prints the job file error to stderr and exits with the appropriate status.
 */
void job_file_error(char* fileName) {
    fprintf(stderr, "testuqwordiply: Unable to open job file \"%s\"\n", 
            fileName);
    exit(JOB_FILE_ERROR);
}

/* job_format_error()
 * ------------------
 * Prints the job format error to stderr and exits with the appropriate status.
 */
void job_format_error(int lineNum, char* fileName) {
    fprintf(stderr, "testuqwordiply: syntax error on line %d of \"%s\"\n", 
            lineNum, fileName);
    exit(JOB_FORMAT_ERROR);

}

/* input_file_error()
 * -----------------
 * Prints out the input file error and exits with the appropriate status. Also
 * frees the memory allocated for the input file name.
 */
void input_file_error(char* inputFileName, int lineNum, char* fileName) {
    fprintf(stderr, "testuqwordiply: unable to open file \"%s\" specified on "
            "line %d of \"%s\"\n", inputFileName, lineNum, fileName);
    free(inputFileName);
    exit(INPUT_FILE_ERROR);
}

/* blank_job_error()
 * -----------------
 * Prints out the blank job error and exits with the appropriate status.
 */
void blank_job_error(char* fileName) {
    fprintf(stderr, "testuqwordiply: no jobs found in \"%s\"\n", fileName);
    exit(BLANK_JOB_ERROR);
}
