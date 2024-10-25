#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

// Global constants
#define INITIAL_TOKEN_SIZE 64    // Initial size of token array
#define INITIAL_INPUT_SIZE 256   // Initial size for input buffer

// Global variables
char *last_command = NULL;       // Stores the last executed command
int first_command = 1;           // Flag to track if the first command is being executed

// Function declarations
void process_commands(char* input);
char** tokenize(char* input);
void execute_command(char** args, char* input_file, char* output_file);
void execute_pipe(char*** commands, int num_commands, char* input_file, char* output_file);
void command_help(void);
void command_cd(char **args);
void command_source(char *filename);
void command_prev(void);
void save_last_command(char *input);
void cleanup_last_command(void);
char** allocate_tokens(int size);
char** resize_tokens(char** tokens, int *size);

/**
 * Allocates memory for a token array.
 * This function is used to initialize an array that will store parsed tokens from the input.
 * @param size - Initial size of the token array
 * @return Pointer to the allocated token array
 */
char** allocate_tokens(int size) {
    return (char **)malloc(size * sizeof(char *));
}

/**
 * Resizes the token array by double when the size limit is reached.
 * This prevents overflow when the array becomes too small to store more tokens.
 * @param tokens - Existing token array
 * @param size - Pointer to the current size of the array, which will be doubled
 * @return Resized token array or NULL on failure
 */
char** resize_tokens(char** tokens, int *size) {
    *size *= 2;
    // Reallocates memory to accommodate the larger array.
    return (char **)realloc(tokens, (*size) * sizeof(char *));
}

/**
 * Tokenizes an input string into an array of tokens.
 * Handles quotes, spaces, and special shell characters like pipes, redirections, etc.
 * @param input - Command input string to be tokenized
 * @return Array of tokenized strings or NULL on failure
 */
char** tokenize(char* input) {
    int token_size = INITIAL_TOKEN_SIZE;     // Initial size for tokens
    char** tokens = allocate_tokens(token_size);  // Allocate initial space for tokens
    int token_count = 0;                    // Keeps track of the number of tokens

    int in_quotes = 0;                      // Flag to handle quotes
    char buffer[INITIAL_INPUT_SIZE];        // Buffer to store the current token
    int buffer_index = 0;                   // Index to keep track of buffer position

    // Loop through each character in the input string
    for (int i = 0; input[i] != '\0'; i++) {
        char c = input[i];

        // Toggles in/out of quotes mode
        if (c == '"') {
            in_quotes = !in_quotes;
            continue;
        }

        // If inside quotes, add the character to the buffer
        if (in_quotes) {
            buffer[buffer_index++] = c;
            continue;
        }

        // Check if the character is a special shell character (e.g., (), <>, |, ;)
        if (strchr("()<>|;", c)) {
            // If the buffer contains a token, save it first before handling special char
            if (buffer_index > 0) {
                buffer[buffer_index] = '\0';  
                tokens[token_count++] = strdup(buffer);  
                buffer_index = 0;  

                // Resize token array if necessary
                if (token_count >= token_size) {
                    tokens = resize_tokens(tokens, &token_size);
                }
            }
            // Handle special character as its own token
            char special_token[2] = {c, '\0'};
            tokens[token_count++] = strdup(special_token);

            // Resize if necessary
            if (token_count >= token_size) {
                tokens = resize_tokens(tokens, &token_size);
            }
            continue;
        }

        // Handles spaces, which are used as token delimiters
        if (isspace(c)) {
            // Save the current token in the buffer (if there is one)
            if (buffer_index > 0) {
                buffer[buffer_index] = '\0';
                tokens[token_count++] = strdup(buffer);
                buffer_index = 0;

                // Resize token array if necessary
                if (token_count >= token_size) {
                    tokens = resize_tokens(tokens, &token_size);
                }
            }
            continue;
        }

        // Add regular characters (non-special, non-space) to the buffer
        buffer[buffer_index++] = c;
    }

    // Adds the last token from the buffer if it hasn't been processed yet
    if (buffer_index > 0) {
        buffer[buffer_index] = '\0';
        tokens[token_count++] = strdup(buffer);

        // Resize if necessary
        if (token_count >= token_size) {
            tokens = resize_tokens(tokens, &token_size);
        }
    }

    // Null-terminate the token array (required for execvp)
    tokens[token_count] = NULL; 
    return tokens;
}

/**
 * Saves the last executed command for the 'prev' command functionality.
 * Ensures that the last entered command is stored for reuse.
 * @param input - The command string to be saved
 */
void save_last_command(char *input) {
    if (input && strlen(input) > 0) {
        // Free previously saved command to avoid memory leak
        if (last_command != NULL) {
            free(last_command);
        }
        // Duplicate and save the new command
        last_command = strdup(input);
        if (last_command == NULL) {
            // Handle memory allocation failure
            fprintf(stderr, "Error: Memory allocation failed while saving the command.\n");
            exit(EXIT_FAILURE);
        }
    }
}

/**
 * Executes the previously saved command if available.
 * The command is stored in 'last_command' and can be re-executed on demand.
 */
void command_prev(void) {
    if (last_command && strlen(last_command) > 0) {
        // Create a copy of the saved command to avoid modifying the original
        char *command_copy = strdup(last_command);
        if (command_copy == NULL) {
            // Handle memory allocation failure
            fprintf(stderr, "Error: Memory allocation failed while copying the command.\n");
            exit(EXIT_FAILURE);
        }
        // Process the copied command (re-execute it)
        process_commands(command_copy);
        free(command_copy);  // Free the copied command after processing
    } else {
        printf("No previous command found.\n");  // Inform the user if no command is saved
    }
}

/**
 * Frees the memory allocated for the last_command variable to avoid memory leaks.
 */
void cleanup_last_command(void) {
    if (last_command != NULL) {
        free(last_command);  // Free the memory allocated for the last command
        last_command = NULL; // Reset the pointer
    }
}

/**
 * Displays help information for built-in commands.
 * This provides a brief overview of available built-in shell commands.
 */
void command_help(void) {
    printf("Available built-in commands:\n");
    printf("cd [path] - Change directory\n");
    printf("source [filename] - Execute script\n");
    printf("prev - Repeat previous command\n");
    printf("help - Show this help message\n");
    printf("exit - Exit the shell\n");
}

/**
 * Changes the current working directory.
 * If no path is specified, it defaults to the home directory.
 * @param args - Array of arguments including the target directory
 */
void command_cd(char **args) {
    if (args[1] == NULL) {
        // No path provided, change to home directory
        chdir(getenv("HOME"));
    } else {
        // Attempt to change to the specified directory
        if (chdir(args[1]) != 0) {
            // Display an error if the directory does not exist
            fprintf(stderr, "cd: No such file or directory: %s\n", args[1]);
        }
    }
}

/**
 * Executes commands from a file line by line
 * Reads each line from the file, processes it as a command, and executes it.
 * @param filename - Name of the file containing commands
 */
void command_source(char *filename) {
    if (!filename) {
        // Print error if no filename is provided
        fprintf(stderr, "source: Missing filename\n");
        return;
    }

    // Open the file for reading
    FILE *file = fopen(filename, "r");
    if (!file) {
        // Print error if the file does not exist
        fprintf(stderr, "source: No such file: %s\n", filename);
        return;
    }

    char line[INITIAL_INPUT_SIZE];

    // Read each line from the file and process it as a command
    while (fgets(line, sizeof(line), file)) {
        // Remove trailing newline character from the line
        line[strcspn(line, "\n")] = 0;
        first_command = 0;  
        process_commands(line);  
    }

    // Close the file after reading all commands
    fclose(file);
}

/**
 * Executes multiple commands connected by pipes with optional I/O redirection
 * @param commands - Array of command arrays
 * @param num_commands - Number of commands in the pipeline
 * @param input_file - File for input redirection (optional)
 * @param output_file - File for output redirection (optional)
 */
void execute_pipe(char*** commands, int num_commands, char* input_file, char* output_file) {
    int input_fd = STDIN_FILENO;
    int pipe_fds[2];
    pid_t pids[num_commands];

    if (input_file) {
        input_fd = open(input_file, O_RDONLY);
        if (input_fd < 0) {
            perror("Cannot open input file");
            exit(1);
        }
    }

    for (int i = 0; i < num_commands; i++) {
        if (i < num_commands - 1) {
            if (pipe(pipe_fds) == -1) {
                perror("pipe failed");
                exit(1);
            }
        }

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork failed");
            exit(1);
        } else if (pid == 0) {
            // Child process logic for piping and redirection
            if (input_fd != STDIN_FILENO) {
                dup2(input_fd, STDIN_FILENO);
                close(input_fd);
            }

            if (i < num_commands - 1) {
                close(pipe_fds[0]);
                dup2(pipe_fds[1], STDOUT_FILENO);
                close(pipe_fds[1]);
            } else if (output_file) {
                int fd_out = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd_out < 0) {
                    perror("Cannot open output file");
                    exit(1);
                }
                dup2(fd_out, STDOUT_FILENO);
                close(fd_out);
            }

            if (execvp(commands[i][0], commands[i]) == -1) {
                perror("command execution failed");
                exit(1);
            }
        }

        // Parent process: manages file descriptors
        pids[i] = pid;
        if (input_fd != STDIN_FILENO) {
            close(input_fd);
        }
        if (i < num_commands - 1) {
            close(pipe_fds[1]);
            input_fd = pipe_fds[0];
        }
    }

    if (input_fd != STDIN_FILENO) {
        close(input_fd);
    }

       // Wait for all child processes to complete
    for (int i = 0; i < num_commands; i++) {
        waitpid(pids[i], NULL, 0);
    }
}

/**
 * Executes multiple commands connected by pipes with optional I/O redirection
 * Manages piping between commands and handles input/output redirection.
 * @param commands - Array of command arrays (each representing a command)
 * @param num_commands - Number of commands in the pipeline
 * @param input_file - File for input redirection (optional)
 * @param output_file - File for output redirection (optional)
 */
void execute_pipe(char*** commands, int num_commands, char* input_file, char* output_file) {
    int input_fd = STDIN_FILENO;  // Initialize input descriptor to standard input
    int pipe_fds[2];              // Array for pipe file descriptors
    pid_t pids[num_commands];      // Array to store process IDs for each command

    // If an input file is specified, open it for reading
    if (input_file) {
        input_fd = open(input_file, O_RDONLY);
        if (input_fd < 0) {
            perror("Cannot open input file");
            exit(1);
        }
    }

    // Loop through all commands in the pipeline
    for (int i = 0; i < num_commands; i++) {
        if (i < num_commands - 1) {
            // Create a pipe for all but the last command
            if (pipe(pipe_fds) == -1) {
                perror("pipe failed");
                exit(1);
            }
        }

        // Fork a child process for each command
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork failed");
            exit(1);
        } else if (pid == 0) {
            // Child process logic for handling pipes and redirection

            if (input_fd != STDIN_FILENO) {
                dup2(input_fd, STDIN_FILENO);  // Redirect input if necessary
                close(input_fd);
            }

            if (i < num_commands - 1) {
                // Redirect output to the write-end of the pipe for all but the last command
                close(pipe_fds[0]);
                dup2(pipe_fds[1], STDOUT_FILENO);
                close(pipe_fds[1]);
            } else if (output_file) {
                // If an output file is specified, open it for writing
                int fd_out = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd_out < 0) {
                    perror("Cannot open output file");
                    exit(1);
                }
                dup2(fd_out, STDOUT_FILENO);  // Redirect output to the file
                close(fd_out);
            }

            // Execute the command using execvp
            if (execvp(commands[i][0], commands[i]) == -1) {
                perror("command execution failed");
                exit(1);
            }
        }

        // Parent process: close file descriptors and manage pipes
        pids[i] = pid;
        if (input_fd != STDIN_FILENO) {
            close(input_fd);
        }
        if (i < num_commands - 1) {
            close(pipe_fds[1]);
            input_fd = pipe_fds[0];  // Set input_fd to the read-end of the current pipe
        }
    }

    if (input_fd != STDIN_FILENO) {
        close(input_fd);  // Close the last input descriptor
    }

    // Wait for all child processes to finish
    for (int i = 0; i < num_commands; i++) {
        waitpid(pids[i], NULL, 0);
    }
}

/**
 * Processes and executes commands based on user input
 * Handles multiple commands separated by semicolons and supports piping and redirection.
 * @param input - The command input string to process
 */
void process_commands(char* input) {
    if (strcmp(input, "prev") != 0) {
        save_last_command(input);  // Save the command to history if not "prev"
    }

    // Split the input by semicolons to handle multiple commands
    char* command = strtok(input, ";");
    while (command != NULL) {
        // Trim leading and trailing whitespace from the command
        while (isspace(*command)) command++;
        char* end = command + strlen(command) - 1;
        while (end > command && isspace(*end)) end--;
        *(end + 1) = '\0';

        // Handle input/output redirection
        char *input_file = NULL, *output_file = NULL;
        char *input_redirect_pos = strchr(command, '<');
        char *output_redirect_pos = strchr(command, '>');

        if (input_redirect_pos) {
            *input_redirect_pos = '\0';  // Split the command at input redirection
            input_file = strtok(input_redirect_pos + 1, " \t");  // Get the input file
        }

        if (output_redirect_pos) {
            *output_redirect_pos = '\0';  // Split the command at output redirection
            output_file = strtok(output_redirect_pos + 1, " \t");  // Get the output file
        }

        // Count pipes in the command
        int num_pipes = 0;
        char* temp = command;
        while ((temp = strchr(temp, '|')) != NULL) {
            num_pipes++;
            temp++;
        }

        if (num_pipes > 0) {
            // Handle piping if there are pipes in the command
            char*** pipe_commands = malloc((num_pipes + 1) * sizeof(char**));
            if (!pipe_commands) {
                perror("malloc failed");
                exit(EXIT_FAILURE);
            }

            // Split the command by pipes and tokenize each subcommand
            char* pipe_command = strtok(command, "|");
            int index = 0;

            while (pipe_command != NULL) {
                pipe_commands[index++] = tokenize(pipe_command);
                pipe_command = strtok(NULL, "|");
            }

            // Execute the piped commands
            execute_pipe(pipe_commands, num_pipes + 1, input_file, output_file);

            // Free memory allocated for pipe commands
            for (int i = 0; i < num_pipes + 1; i++) {
                char** args = pipe_commands[i];
                if (args) {
                    for (int j = 0; args[j] != NULL; j++) {
                        free(args[j]);
                    }
                    free(args);
                }
            }
            free(pipe_commands);
        } else {
            // Handle a single command with optional I/O redirection
            char** args = tokenize(command);
            execute_command(args, input_file, output_file);

            // Free memory allocated for arguments
            for (int i = 0; args[i] != NULL; i++) {
                free(args[i]);
            }
            free(args);
        }

        // Get the next command in the input (if any)
        command = strtok(NULL, ";");
    }
}

/**
 * Main function: Shell entry point
 * Initializes the shell, reads input, and processes commands until exit.
 */
int main(int argc, char* argv[]) {
    initialize_shell();  // Initialize the shell environment

    while (1) {
        // Display the shell prompt (custom prompt based on first command)
        if (first_command) {
            printf("osh> ");
        } else {
            printf("osh-next> ");
        }

        // Read input from stdin
        char* input = read_input();
        if (!input) {
            fprintf(stderr, "Failed to read input\n");
            continue;
        }

        process_commands(input);     // Process and execute the command(s) from the input

        free(input);        // Free the input buffer
    }

    return 0;  // Exit the shell
}


