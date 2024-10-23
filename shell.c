#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

// initalizing global variables
#define INITIAL_TOKEN_SIZE 64
#define INITIAL_INPUT_SIZE 256
char *last_command = NULL;
int first_command = 1;  // Flag to track first command

// Forward declarations
void process_commands(char* input);
char** tokenize(char* input);
void execute_command(char** args, char* input_file, char* output_file);
void execute_pipe(char*** commands, int num_commands);
void command_help(void);
void command_cd(char **args);
void command_source(char *filename);
void command_prev(void);
void save_last_command(char *input);
void cleanup_last_command(void);
char** allocate_tokens(int size);
char** resize_tokens(char** tokens, int *size);

/**
 * Allocates memory for token array
 */
char** allocate_tokens(int size) {
    return (char **)malloc(size * sizeof(char *));
}

/**
 * Resizes token array when needed
 */
char** resize_tokens(char** tokens, int *size) {
    *size *= 2;
    return (char **)realloc(tokens, (*size) * sizeof(char *));
}

/**
 * Tokenizes input string into array of strings
 * Handles quotes, spaces, and special characters
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
      
        if (c == '"') {
            in_quotes = !in_quotes;
            continue;
        }
        
        if (in_quotes) {
            buffer[buffer_index++] = c;
            continue;
        }
        
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
        
        buffer[buffer_index++] = c;
    }
    
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
 * Saves the last executed command
 */
void save_last_command(char *input) {
    // Check if the input is valid and non-empty
    if (input && strlen(input) > 0) {
        // Free the existing last_command if it exists
        if (last_command != NULL) {
            free(last_command);
        }

        // Duplicate the input and assign it to last_command
        last_command = strdup(input);

        // Ensure memory allocation succeeded
        if (last_command == NULL) {
            fprintf(stderr, "Error: Memory allocation failed while saving the command.\n");
            exit(EXIT_FAILURE);
        }
    }
}

/**
 * Executes the previous command
 */
void command_prev(void) {
    // Check if a previous command exists
    if (last_command && strlen(last_command) > 0) {
        // Make a copy of the last command for safe execution
        char *command_copy = strdup(last_command);

        // Ensure memory allocation succeeded
        if (command_copy == NULL) {
            fprintf(stderr, "Error: Memory allocation failed while copying the command.\n");
            exit(EXIT_FAILURE);
        }

        // Process the copied command (but avoid saving 'prev' again)
        process_commands(command_copy);

        // Free the copied command after it has been processed
        free(command_copy);
    } else {
        printf("No previous command found.\n");
    }
}

/**
 * Frees the memory allocated for the last_command variable before exiting the shell.
 * This function should be called when the shell exits to clean up resources.
 */
void cleanup_last_command(void) {
    if (last_command != NULL) {
        free(last_command);
        last_command = NULL;
    }
}

/**
 * Displays help information
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
 * Changes current directory
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
 * Executes commands from a file
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
        first_command = 0;  // Prevent welcome message
        process_commands(line);
    }
    fclose(file);
}

/**
 * Executes multiple commands connected by pipes with proper input and output redirection.
 */
void execute_pipe(char*** commands, int num_commands) {
    int pipe_fds[2];
    int input_fd = 0; // Initial input is standard input (stdin)

    for (int i = 0; i < num_commands; i++) {
        // Create a pipe for all but the last command
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
            // Child process

            // If not the first command, set the input to the previous pipe's read end
            if (input_fd != 0) {
                dup2(input_fd, STDIN_FILENO);
                close(input_fd);
            }

            // If not the last command, set the output to the current pipe's write end
            if (i < num_commands - 1) {
                close(pipe_fds[0]); // Close read end
                dup2(pipe_fds[1], STDOUT_FILENO);
                close(pipe_fds[1]);
            }

            // Execute the command
            if (execvp(commands[i][0], commands[i]) == -1) {
                fprintf(stderr, "%s: command not found\n", commands[i][0]);
                exit(1);
            }
        }

        // Parent process

        // Close the write end of the pipe in the parent
        if (i < num_commands - 1) {
            close(pipe_fds[1]);
        }

        // Update input_fd to the read end of the current pipe for the next command
        input_fd = pipe_fds[0];
    }

    // Close the last read end if it's open
    if (input_fd != 0) {
        close(input_fd);
    }

    // Wait for all child processes to complete
    for (int i = 0; i < num_commands; i++) {
        wait(NULL);
    }
}


/**
 * Executes a single command with optional I/O redirection
 */
void execute_command(char** args, char* input_file, char* output_file) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("Fork Failed");
        exit(1);
    } 
    else if (pid == 0) {
        // Handle input redirection
        if (input_file) {
            int fd_in = open(input_file, O_RDONLY);
            if (fd_in < 0) {
                fprintf(stderr, "Cannot open file: %s\n", input_file);
                exit(1);
            }
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }

        // Handle output redirection
        if (output_file) {
            int fd_out = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd_out < 0) {
                fprintf(stderr, "Cannot open file: %s\n", output_file);
                exit(1);
            }
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
        }

        // Execute the command
        if (execvp(args[0], args) == -1) {
            fprintf(stderr, "%s: command not found\n", args[0]);
            exit(1);
        }
    } else {
        waitpid(pid, NULL, 0);
    }
}


/**
 * Process command function modification to handle multiple pipes
 */
void process_commands(char* input) {
    // Avoid saving the 'prev' command itself as the last command to prevent recursion
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

        if (input_redirect_pos && strchr(input_redirect_pos + 1, '<')) {
            fprintf(stderr, "Error: Multiple input redirections.\n");
            return;
        }

        if (output_redirect_pos && strchr(output_redirect_pos + 1, '>')) {
            fprintf(stderr, "Error: Multiple output redirections.\n");
            return;
        }

        if (input_redirect_pos) {
            *input_redirect_pos = '\0';
            input_file = strtok(input_redirect_pos + 1, " \t");
        }

        if (output_redirect_pos) {
            *output_redirect_pos = '\0';
            output_file = strtok(output_redirect_pos + 1, " \t");
        }

        // Count the number of pipe segments
        int num_pipes = 0;
        char* temp = command;
        while ((temp = strchr(temp, '|')) != NULL) {
            num_pipes++;
            temp++;
        }

        // Handle pipeline commands
        if (num_pipes > 0) {
            char** pipe_commands[num_pipes + 1];
            char* pipe_command = strtok(command, "|");
            int index = 0;

            while (pipe_command != NULL) {
                pipe_commands[index++] = tokenize(pipe_command);
                pipe_command = strtok(NULL, "|");
            }

            execute_pipe(pipe_commands, num_pipes + 1);

            // Free the tokenized commands
            for (int i = 0; i < num_pipes + 1; i++) {
                char** args = pipe_commands[i];
                for (int j = 0; args[j] != NULL; j++) {
                    free(args[j]);
                }
                free(args);
            }
        } else {
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

            for (int i = 0; args[i] != NULL; i++) free(args[i]);
            free(args);
        }

        command = strtok(NULL, ";");
    }
}

int main(int argc, char **argv) {
    char input[INITIAL_INPUT_SIZE];
    printf("Welcome to mini-shell\n");  // Removed period to match test

    while (1) {
        printf("shell $ ");
        fflush(stdout);

        if ((fgets(input, INITIAL_INPUT_SIZE, stdin) == NULL) || (strcmp(input, "exit\n") == 0)) {
            printf("Bye bye.\n");
            break;
        }

        input[strcspn(input, "\n")] = 0;
        first_command = 0;  // Mark that we're past the first command
        process_commands(input);
    }

    cleanup_last_command();

    return 0;
}
