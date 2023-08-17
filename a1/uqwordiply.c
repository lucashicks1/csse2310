#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <csse2310a1.h>

//Error Codes
#define ERROR_INVALID_CL 1
#define ERROR_INVALID_STARTER 2
#define ERROR_INVALID_DICTIONARY 3
#define EOF_NO_GUESSES 4

//Error Messages
#define MESSAGE_INVALID_CL "Usage: uqwordiply [--start starter-word | --len "\
    "length] [--dictionary filename]\n"
#define MESSAGE_INVALID_STARTER "uqwordiply: invalid starter word\n"
#define MESSAGE_INVALID_DICTIONARY "uqwordiply: dictionary file \"%s\" "\
    "cannot be opened\n"
#define MESSAGE_ONLY_LETTERS "Guesses must contain only letters - try again.\n"
#define MESSAGE_CONTAIN_STARTER "Guesses must contain the starter word - "\
    "try again.\n"
#define MESSAGE_IS_STARTER "Guesses can't be the starter word - try again.\n"
#define MESSAGE_NOT_IN_DICT "Guess not found in dictionary - try again.\n"
#define MESSAGE_ALREADY_GUESSED "You've already guessed that word - try "\
    "again.\n"

//Constants
#define MAX_GUESSES 5
#define DEFAULT_DICT "/usr/share/dict/words"
#define MAX_WORD_SIZE 50
#define WELCOME_MESSAGE "Welcome to UQWordiply!\nThe starter word is: %s\n"\
    "Enter words containing this word.\n"
#define GUESS_PROMPT "Enter guess %d:\n"

//Stores info on the wordiply game
typedef struct {
    int guessNum;
    char** guesses;
    const char* starterWord;
    char** dictionary;
    int numDictionaryWords;
    int longestDictWord;
    int longestGuessLength;
    int totalGuessLetters;
} Game;

//Prototypes
void exit_program(Game* game, int code);
int parse_length(char* length, bool lenStartUsed, Game* game);
void parse_starter_word(char* word, bool lenStartUsed, Game* game);
FILE* parse_dictionary(char* dict, bool dictUsed, Game* game);
void parse_args(int argc, char** argv, Game* game);
void generate_dictionary(FILE* file, Game* game);
bool contains_starter_word(char* word, const char* starterWord);
void upper_word(char* word);
bool contains_non_alpha(char* word);
bool is_in_dictionary(char* word, char** dictionary, int numWords);
void remove_newline(char* word);
bool already_guessed(char* guess, int guessNum, char** guesses);
bool valid_guess(char* guess, Game* game);
void add_guess(char* guess, Game* game);
void display_game_stats(Game* game);
void free_stored_words(char** dictionary, int numWords);
void free_game_contents(Game* game);
void game_loop(Game* game);

/* main()
 * ------
 * Main function that is called at the start. This function initialises any
 * important variables and orchestrates the overall flow of the game.
 *
 * argc: number of arguments passed to the program
 * argv: arguments passed to the program
 *
 * Returns: indication of how the program ended
 */
int main(int argc, char** argv) {
    Game* game = malloc(sizeof(Game));
    game->numDictionaryWords = 0;
    game->guesses = malloc(0);
    game->dictionary = malloc(0);

    parse_args(argc, argv, game);
    printf(WELCOME_MESSAGE, game->starterWord);
    game_loop(game);

    return 0;
}

/* exit_program()
 * ----------------------
 * Frees memory allocated to game struct and its variables. Prints out 
 * appropriate error message and exits program with appropriate error code.
 *
 * game: struct to store game information
 * code: error code to be exited with
 */

void exit_program(Game* game, int code) {
    // Free memory for game struct
    free(game->dictionary);
    free(game->guesses);
    free(game);

    // Prints error message based on error code
    switch (code) {
        case ERROR_INVALID_CL:
            fprintf(stderr, MESSAGE_INVALID_CL);
            break;
        case ERROR_INVALID_STARTER:
            fprintf(stderr, MESSAGE_INVALID_STARTER);
            break;
        default:
            break;
    }

    exit(code);
}

/* parse_length()
 * --------------
 * Validates --len command line argument value. Checks whether value is 
 * either 3 or 4. Also checks whether --len or --start has been provided
 * already.
 *
 * length: starter word length provided in command line
 * lenStartUsed: whether the --len or --start argument has been used before
 * game: struct to store game information
 *
 * Returns: integer value of the starter word length
 */
int parse_length(char* length, bool lenStartUsed, Game* game) {
    // Checks if --len or --start was used previously
    if (lenStartUsed) {
        exit_program(game, ERROR_INVALID_CL);
    }
    if (strlen(length) != 1) {
        exit_program(game, ERROR_INVALID_CL);
    }
    int len = atoi(length);

    if ((len != 3) && (len != 4)) {
        exit_program(game, ERROR_INVALID_CL);
    }
    return len;
}

/* parse_starter_word()
 * --------------------
 * Validates --start command line argument value. Checks if word contains any 
 * non-letters, and whether its length is 3 or 4 characters. Checks if --len
 * or --start has been provided already.
 * 
 * word: starter word provided in command line
 * lenStartUsed: whether the --len or --start argument has been used before
 * game: struct to store game information
 */
void parse_starter_word(char* word, bool lenStartUsed, Game* game) {
    if (lenStartUsed) {
        exit_program(game, ERROR_INVALID_CL);
    }
    int i = 0;
    // Check if starter word contains any non-letters
    for (; word[i] != '\0'; ++i) {
        if (!isalpha(word[i])) {
            exit_program(game, ERROR_INVALID_STARTER);
        }
    }
    // Check starter word length
    if ((i != 3) && (i != 4)) {
        exit_program(game, ERROR_INVALID_STARTER);
    }
}

/* parse_dictionary()
 * ------------------
 * Checks whether the given file path in --dictionary is valid for reading. If
 * invalid, file may not exist or file is unable to be read due to permissions.
 *
 * dict: file name of dictionary
 * dictUsed: whether --dictionary argument has been used before
 * game: struct to store game information
 *
 * Returns: file object containing all file information
 */
FILE* parse_dictionary(char* dict, bool dictUsed, Game* game) {
    // Checks if --dictionary arguments was already provided
    if (dictUsed) {
        exit_program(game, ERROR_INVALID_CL);
    }
    // Open file for reading
    FILE* file;
    file = fopen(dict, "r");

    // Check if file opened successfully
    if (!file) {
        fprintf(stderr, MESSAGE_INVALID_DICTIONARY, dict);
        exit_program(game, ERROR_INVALID_DICTIONARY);
    }
    return file;
}

/* parse_args()
 * ------------
 * Interprets any command line arguments given and calls parse functions to 
 * validate the given command line argument values.
 *
 * argc: number of arguments passed to the program
 * argv: arguments passed to the program
 */
void parse_args(int argc, char** argv, Game* game) {
    //Detects missing values for command line arguments
    if (argc % 2 == 0) {
        exit_program(game, ERROR_INVALID_CL);
    }
    //Used in checking if command is duplicated or both start and len are used
    bool lenStartUsed = false;
    bool dictUsed = false;
    int len = 0;
    FILE* file;
    game->starterWord = "";

    // Checks presence of command line arguments
    for (int i = 1; i < argc; i += 2) {
        if (strcmp(argv[i], "--len") == 0) {
            len = parse_length(argv[i + 1], lenStartUsed, game);
            lenStartUsed = true;
        } else if (strcmp(argv[i], "--start") == 0) {
            parse_starter_word(argv[i + 1], lenStartUsed, game);
            upper_word(argv[i + 1]);
            game->starterWord = argv[i + 1];
            lenStartUsed = true;
        } else if (strcmp(argv[i], "--dictionary") == 0) {
            // Checks whether the dictionary can be opened
            file = parse_dictionary(argv[i + 1], dictUsed, game);
            dictUsed = true;
        } else {
            exit_program(game, ERROR_INVALID_CL);
        }
    }
    // Sets default dictionary and/or starter word if no args are passed
    if (!dictUsed) {
	file = fopen(DEFAULT_DICT, "r");
    }

    if (!strlen(game->starterWord)) {
        game->starterWord = get_wordiply_starter_word(len);
    }

    generate_dictionary(file, game);
}

/* generate_dictionary()
 * ---------------------
 * Reads a given dictionary file and generates a subset of those words to be 
 * used in the game. Only words that contain the starter word are passed to the
 * dictionary.
 *
 * file: file object containing all file information
 * game: struct to store game information
 */
void generate_dictionary(FILE* file, Game* game) {
    char* line = (char*) malloc(sizeof(char) * (MAX_WORD_SIZE + 1));
    int numElements = 0;
    game->longestDictWord = 0;
    // Goes through file until EOF is reached
    while (fgets(line, MAX_WORD_SIZE, file)) {
        upper_word(line);
        remove_newline(line);

        //If word is valid and has the starter word, add to dictionary
        if (strstr(line, game->starterWord) && !contains_non_alpha(line)) {
            char* newWord = malloc(sizeof(char) * (strlen(line) + 1));
            strcpy(newWord, line);

            game->dictionary = realloc(
                    game->dictionary, sizeof(char*) * (numElements + 1));
            game->dictionary[numElements] = newWord;

            if (game->longestDictWord < strlen(newWord)) {
                game->longestDictWord = strlen(newWord);
            }
            numElements++;
        }
    }
    game->numDictionaryWords = numElements;
    fclose(file);
    free(line);
}

/* contains_starter_word()
 * -----------------------
 * Determines whether the provided word contains the starter word
 *
 * word: word that is checked to contain starter word
 * starterWord: game starter word
 *
 * Returns: true if word contains starter word
 */
bool contains_starter_word(char* word, const char* starterWord) {
    if (strstr(word, starterWord)) {
        return true;
    }
    return false;
}

/* upper_word()
 * ------------
 * Makes every letter in the word uppercase
 *
 * word: word to be capitalised
 */
void upper_word(char* word) {
    for (int i = 0; word[i] != '\0'; i++) {
        word[i] = toupper(word[i]);
    }
}

/* contains_non_alpha()
 * --------------------
 * Determines whether the provided word contains any non-alphabetic characters.
 *
 * word: word to be checked
 *
 * Returns: true if word contains non-alphabetic characters
 */
bool contains_non_alpha(char* word) {
    for (int i = 0; word[i] != '\0'; i++) {
        if (!isalpha(word[i])) {
                return true;
        }
    }
    return false;
}

/* is_in_dictionary()
 * -----------------
 * Determines whether word is in game dictionary
 *
 * word: word to be searched for in dictionary
 * dictionary: dictionary to be searched
 * numWords: number of words in game dictionary
 *
 * Returns: true if word is in dictionary
 */
bool is_in_dictionary(char* word, char** dictionary, int numWords) {
    for (int i = 0; i < numWords; i++) {
        if (strcmp(word, dictionary[i]) == 0) {
            return true;
        }
    }
    return false;
}

/* remove_newline()
 * ----------------
 * Removes any new-line characters in a word and replaces it with 0
 *
 * word: word to be checked for newline characters
 */
void remove_newline(char* word) {
    for (int i = 0; word[i] != '\0'; i++) {
        if (word[i] == '\n') {
            word[i] = 0;
        }
    }
}

/* already_guessed()
 * -----------------
 * Determines whether the provided guess has already been made
 *
 * guess: guess that has been made
 * guessNum: the number of the guess (eg: first guess - Guess 1)
 * guesses: string array of all guesses made in the game
 *
 * Returns: true if guess has already been made in the game
 */
bool already_guessed(char* guess, int guessNum, char** guesses) {
    for (int i = 0; i < (guessNum - 1); i++) {
        if (!strcmp(guess, guesses[i])) {
            return true;
        }
    }
    return false;
}

/* valid_guess()
 * -------------
 * Determines whether an attempted guess is valid or not. Guesses must only 
 * contain letters, must contain starter word, cannot be starter word, must be
 * in the game dictionary and must not already be guessed.
 *
 * guess: attempted guess from the player
 * game: struct to store game information
 *
 * Returns: true if guess is valid
 */
bool valid_guess(char* guess, Game* game) {
    if (contains_non_alpha(guess)){
        printf(MESSAGE_ONLY_LETTERS);
        return false;
    }
    if (!contains_starter_word(guess, game->starterWord)) {
        printf(MESSAGE_CONTAIN_STARTER);
        return false;
    }
    if (!strcmp(guess, game->starterWord)) {
        printf(MESSAGE_IS_STARTER);
        return false;
    }
    if (!is_in_dictionary(guess, game->dictionary, game->numDictionaryWords)) {
        printf(MESSAGE_NOT_IN_DICT);
        return false;
    }
    if (already_guessed(guess, game->guessNum, game->guesses)) {
        printf(MESSAGE_ALREADY_GUESSED);
        return false;
    }
    return true;
}

/* add_guess()
 * -----------
 * Adds given guesses to the list of guesses made in the game, and updates 
 * guess information in the game struct (longest word, total characters, etc.)
 *
 * guess: guess to be added
 * game: struct to store game information
 */
void add_guess(char* guess, Game* game) {
    int guessLength = strlen(guess);

    if (game->longestGuessLength < guessLength) {
        game->longestGuessLength = guessLength;
    }
    game->totalGuessLetters += guessLength;
    game->guesses = realloc(game->guesses, sizeof(char*) * game->guessNum);
    char* word = malloc(sizeof(char) * (guessLength + 1));
    strcpy(word, guess);
    game->guesses[game->guessNum - 1] = word;
}

/* display_game_stats()
 * --------------------
 * Displays the end of game statistics (total length of words, longest words 
 * found, and longest possible words).
 *
 * game: struct to store game information
 */
void display_game_stats(Game* game) {
    printf("\nTotal length of words found: %d\n", game->totalGuessLetters);
    printf("Longest word(s) found:\n");

    for (int i = 0; i < (game->guessNum - 1); i++) {
        if (strlen(game->guesses[i]) == game->longestGuessLength) {
            printf("%s (%d)\n", game->guesses[i], game->longestGuessLength);
        }
    }

    printf("Longest word(s) possible:\n");
    for (int i = 0; i < game->numDictionaryWords; i++) {
        if (strlen(game->dictionary[i]) == game->longestDictWord) {
            printf("%s (%d)\n", game->dictionary[i], game->longestDictWord);
        }
    }
}

/* free_stored_words()
 * -------------------
 * Frees memory allocated for a string array and the strings inside it.
 *
 * dictionary: string array to be freed
 * numWords: number of words in the dictionary
 */
void free_stored_words(char** dictionary, int numWords) {
    for (int i = 0; i < numWords; i++) {
        free(dictionary[i]);
    }
    free(dictionary);
}

/* free_game_contents()
 * --------------------
 * Calls free_stored_words() to free the dictionary and the guesses array, and
 * also frees the game struct.
 *
 * game: struct to store game information
 */
void free_game_contents(Game* game){
    free_stored_words(game->guesses, game->guessNum - 1);
    free_stored_words(game->dictionary, game->numDictionaryWords);
    free(game);
}

/* game_loop()
 * -----------
 * Main game loop that handles guess-making, calls functions to validate
 * this guess, checks for EOF and calls display_game_stats() to show game
 * information at the end
 *
 * game: struct to store game information
 */
void game_loop(Game* game) {
    game->guessNum = 1; 
    game->longestGuessLength = 0;
    game->totalGuessLetters = 0;

    char* guess = malloc(sizeof(char) * (MAX_WORD_SIZE + 1));
    char* out;

    while (game->guessNum <= MAX_GUESSES) {
        printf(GUESS_PROMPT, game->guessNum);

        //Read until newline is detected
        do {
            out = fgets(guess, MAX_WORD_SIZE + 1, stdin);

            //Check if EOF is detected
            if (!out) {
                break;
            }
        } while (!strchr(guess, '\n'));

        //Check if EOF is detected
        if (!out) {
            break;
        }

        upper_word(guess);
        remove_newline(guess);
        
        if (valid_guess(guess, game)) {
            add_guess(guess, game);
            game->guessNum++;
        }

    }

    // No guess was made
    if (game->guessNum == 1) {
        free_game_contents(game);
        free(guess);
        exit(EOF_NO_GUESSES);
    }

    display_game_stats(game);

    free_game_contents(game);
    free(guess);
}
