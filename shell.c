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
 * Allocates memory for a token array
 * @param size - Initial size of the token array
 * @return Pointer to the allocated token array
 */
char** allocate_tokens(int size) {
    return (char **)malloc(size * sizeof(char *));
}

/**
 * Resizes the token array when the size limit is reached
 * @param tokens - Existing token array
 * @param size - Pointer to the current size of the array
 * @return Resized token array or NULL on failure
 */
char** resize_tokens(char** tokens, int *size) {
    *size *= 2;
    return (char **)realloc(tokens, (*size) * sizeof(char *));
}

/**
 * Tokenizes an input string into an array of tokens
 * Handles quotes, spaces, and special shell characters
 * @param input - Command input string to be tokenized
 * @return Array of tokenized strings or NULL on failure
 */
char** tokenize(char* input) {
    int token_size = INITIAL_TOKEN_SIZE;
    char** tokens = allocate_tokens(token_size);
    int token_count = 0;

    int in_quotes = 0;
    char buffer[INITIAL_INPUT_SIZE];
    int buffer_index = 0;

    for (int i = 0; input[i] != '\0'; i++) {
        char c = input[i];

        // Handles quoted strings
        if (c == '"') {
            in_quotes = !in_quotes;
            continue;
        }

        if (in_quotes) {
            buffer[buffer_index++] = c;
            continue;
        }

        // Detect special characters like (), <>, |, and ;
        if (strchr("()<>|;", c)) {
            if (buffer_index > 0) {
                buffer[buffer_index] = '\0';
                tokens[token_count++] = strdup(buffer);
                buffer_index = 0;

                if (token_count >= token_size) {
                    tokens = resize_tokens(tokens, &token_size);
                }
            }
            char special_token[2] = {c, '\0'};
            tokens[token_count++] = strdup(special_token);

            if (token_count >= token_size) {
                tokens = resize_tokens(tokens, &token_size);
            }
            continue;
        }

        // Handles spaces as token separators
        if (isspace(c)) {
            if (buffer_index > 0) {
                buffer[buffer_index] = '\0';
                tokens[token_count++] = strdup(buffer);
                buffer_index = 0;

                if (token_count >= token_size) {
                    tokens = resize_tokens(tokens, &token_size);
                }
            }
            continue;
        }

        // Adds regular characters to the buffer
        buffer[buffer_index++] = c;
    }

    // Adds the last token if any remains in the buffer
    if (buffer_index > 0) {
        buffer[buffer_index] = '\0';
        tokens[token_count++] = strdup(buffer);
        if (token_count >= token_size) {
            tokens = resize_tokens(tokens, &token_size);
        }
    }

    tokens[token_count] = NULL; 
    return tokens;
}

/**
 * Saves the last executed command for repeat functionality
 * @param input - The command string to be saved
 */
void save_last_command(char *input) {
    if (input && strlen(input) > 0) {
        if (last_command != NULL) {
            free(last_command);
        }
        last_command = strdup(input);
        if (last_command == NULL) {
            fprintf(stderr, "Error: Memory allocation failed while saving the command.\n");
            exit(EXIT_FAILURE);
        }
    }
}

/**
 * Executes the previously saved command if available
 */
void command_prev(void) {
    if (last_command && strlen(last_command) > 0) {
        char *command_copy = strdup(last_command);
        if (command_copy == NULL) {
            fprintf(stderr, "Error: Memory allocation failed while copying the command.\n");
            exit(EXIT_FAILURE);
        }
        process_commands(command_copy);
        free(command_copy);
    } else {
        printf("No previous command found.\n");
    }
}

/**
 * Frees the memory allocated for the last_command variable
 * to avoid memory leaks
 */
void cleanup_last_command(void) {
    if (last_command != NULL) {
        free(last_command);
        last_command = NULL;
    }
}

/**
 * Displays help information for built-in commands
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
 * Changes the current working directory
 * @param args - Array of arguments including the target directory
 */
void command_cd(char **args) {
    if (args[1] == NULL) {
        chdir(getenv("HOME"));
    } else {
        if (chdir(args[1]) != 0) {
            fprintf(stderr, "cd: No such file or directory: %s\n", args[1]);
        }
    }
}

/**
 * Executes commands from a file line by line
 * @param filename - Name of the file containing commands
 */
void command_source(char *filename) {
    if (!filename) {
        fprintf(stderr, "source: Missing filename\n");
        return;
    }
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "source: No such file: %s\n", filename);
        return;
    }
    char line[INITIAL_INPUT_SIZE];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = 0;
        first_command = 0;
        process_commands(line);
    }
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
 * Executes a single command with optional I/O redirection
 * @param args - Array containing command and its arguments
 * @param input_file - Input file for redirection (optional)
 * @param output_file - Output file for redirection (optional)
 */
void execute_command(char** args, char* input_file, char* output_file) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("Fork Failed");
        exit(1);
    } else if (pid == 0) {
        // In the child process
        if (input_file) {
            int fd_in = open(input_file, O_RDONLY);
            if (fd_in < 0) {
                perror("Cannot open input file");
                exit(1);
            }
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }

        if (output_file) {
            int fd_out = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd_out < 0) {
                perror("Cannot open output file");
                exit(1);
            }
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
        }

        if (execvp(args[0], args) == -1) {
            perror("command execution failed");
            exit(1);
        }
    } else {
        // In the parent process, wait for the child to finish
        waitpid(pid, NULL, 0);
    }
}

/**
 * Processes and executes commands based on user input
 * Handles multiple commands separated by pipes and supports redirection
 * @param input - The command input string to process
 */
void process_commands(char* input) {
    if (strcmp(input, "prev") != 0) {
        save_last_command(input);
    }

    char* command = strtok(input, ";");
    while (command != NULL) {
        while (isspace(*command)) command++;
        char* end = command + strlen(command) - 1;
        while (end > command && isspace(*end)) end--;
        *(end + 1) = '\0';

        char *input_file = NULL, *output_file = NULL;
        char *input_redirect_pos = strchr(command, '<');
        char *output_redirect_pos = strchr(command, '>');

        if (input_redirect_pos) {
            *input_redirect_pos = '\0';
            input_file = strtok(input_redirect_pos + 1, " \t");
        }

        if (output_redirect_pos) {
            *output_redirect_pos = '\0';
            output_file = strtok(output_redirect_pos + 1, " \t");
        }

        int num_pipes = 0;
        char* temp = command;
        while ((temp = strchr(temp, '|')) != NULL) {
            num_pipes++;
            temp++;
        }

        if (num_pipes > 0) {
            char*** pipe_commands = malloc((num_pipes + 1) * sizeof(char**));
            if (!pipe_commands) {
                perror("malloc failed");
                exit(EXIT_FAILURE);
            }

            char* pipe_command = strtok(command, "|");
            int index = 0;

            while (pipe_command != NULL) {
                pipe_commands[index++] = tokenize(pipe_command);
                pipe_command = strtok(NULL, "|");
            }

            execute_pipe(pipe_commands, num_pipes + 1, input_file, output_file);

            // Freeing memory allocated for pipe commands
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
            // Handle single command
            char** args = tokenize(command);

            if (args[0] != NULL) {
                if (strcmp(args[0], "help") == 0) {
                    command_help();
                } else if (strcmp(args[0], "prev") == 0) {
                    command_prev();
                } else if (strcmp(args[0], "source") == 0) {
                    command_source(args[1]);
                } else if (strcmp(args[0], "cd") == 0) {
                    command_cd(args);
                } else {
                    execute_command(args, input_file, output_file);
                }
            }

            // Free tokens after execution
            for (int i = 0; args[i] != NULL; i++) free(args[i]);
            free(args);
        }

        command = strtok(NULL, ";");
    }
}

/**
 * Main function: Initializes the shell and processes user input in a loop
 */
int main(int argc, char **argv) {
    char input[INITIAL_INPUT_SIZE];
    printf("Welcome to mini-shell\n");

    while (1) {
        printf("shell $ ");
        fflush(stdout);

        if ((fgets(input, INITIAL_INPUT_SIZE, stdin) == NULL) || (strcmp(input, "exit\n") == 0)) {
            printf("Bye bye.\n");
            break;
        }

        // Remove the newline character from input
        input[strcspn(input, "\n")] = 0;
        first_command = 0;
        process_commands(input);
    }

    cleanup_last_command();
    return 0;
}

