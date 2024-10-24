#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

// Initializing global variables
#define INITIAL_TOKEN_SIZE 64
#define INITIAL_INPUT_SIZE 256
char *last_command = NULL;
int first_command = 1;  // Flag to track first command
int in_prev_command = 0;  // Flag to prevent prev recursion

// Forward declarations
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
    if (input && strlen(input) > 0 && !in_prev_command) {
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
 * Executes the previous command
 */
void command_prev(void) {
    if (last_command && strlen(last_command) > 0) {
        char *command_copy = strdup(last_command);
        if (command_copy == NULL) {
            fprintf(stderr, "Error: Memory allocation failed while copying the command.\n");
            exit(EXIT_FAILURE);
        }
        in_prev_command = 1;  // Set flag before processing
        process_commands(command_copy);
        in_prev_command = 0;  // Reset flag after processing
        free(command_copy);
    } else {
        printf("No previous command found.\n");
    }
}

/**
 * Frees the memory allocated for the last_command variable
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
        first_command = 0;
        process_commands(line);
    }
    fclose(file);
}

/**
 * Executes a piped command sequence
 */
void execute_pipe(char*** commands, int num_commands, char* input_file, char* output_file) {
    int pipes[num_commands - 1][2];
    
    // Create all necessary pipes
    for (int i = 0; i < num_commands - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe failed");
            exit(1);
        }
    }

    for (int i = 0; i < num_commands; i++) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork failed");
            exit(1);
        }
        
        if (pid == 0) {  // Child process
            // Handle input redirection for first command
            if (i == 0 && input_file) {
                int fd = open(input_file, O_RDONLY);
                if (fd == -1) {
                    perror("open input failed");
                    exit(1);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }
            
            // Handle output redirection for last command
            if (i == num_commands - 1 && output_file) {
                int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd == -1) {
                    perror("open output failed");
                    exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }
            
            // Connect pipes
            if (i > 0) {  // Not first command
                dup2(pipes[i-1][0], STDIN_FILENO);
            }
            if (i < num_commands - 1) {  // Not last command
                dup2(pipes[i][1], STDOUT_FILENO);
            }
            
            // Close all pipe fds
            for (int j = 0; j < num_commands - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            
            execvp(commands[i][0], commands[i]);
            perror("execvp failed");
            exit(1);
        }
    }
    
    // Parent closes all pipe fds
    for (int i = 0; i < num_commands - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    
    // Wait for all children
    for (int i = 0; i < num_commands; i++) {
        wait(NULL);
    }
}

/**
 * Executes a single command with I/O redirection
 */
void execute_command(char** args, char* input_file, char* output_file) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("Fork Failed");
        exit(1);
    } 
    else if (pid == 0) {
        if (input_file) {
            int fd_in = open(input_file, O_RDONLY);
            if (fd_in < 0) {
                fprintf(stderr, "Cannot open file: %s\n", input_file);
                exit(1);
            }
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }

        if (output_file) {
            int fd_out = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd_out < 0) {
                fprintf(stderr, "Cannot open file: %s\n", output_file);
                exit(1);
            }
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
        }

        if (execvp(args[0], args) == -1) {
            fprintf(stderr, "%s: command not found\n", args[0]);
            exit(1);
        }
    } else {
        waitpid(pid, NULL, 0);
    }
}

/**
 * Process and execute commands
 */
void process_commands(char* input) {
    if (!in_prev_command && strcmp(input, "prev") != 0) {
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
            char** pipe_commands[num_pipes + 1];
            char* pipe_command = strtok(command, "|");
            int index = 0;

            while (pipe_command != NULL) {
                pipe_commands[index++] = tokenize(pipe_command);
                pipe_command = strtok(NULL, "|");
            }

            execute_pipe(pipe_commands, num_pipes + 1, input_file, output_file);

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
    printf("Welcome to mini-shell\n");

    while (1) {
        printf("shell $ ");
        fflush(stdout);

        if ((fgets(input, INITIAL_INPUT_SIZE, stdin) == NULL) || (strcmp(input, "exit\n") == 0)) {
            printf("Bye bye.\n");
            break;
        }

        input[strcspn(input, "\n")] = 0;
        first_command = 0;
        process_commands(input);
    }

    cleanup_last_command();
    return 0;
}