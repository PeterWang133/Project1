// importing important libraries
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
void execute_pipe(char** args1, char** args2);
void command_help(void);
void command_cd(char **args);
void command_source(char *filename);
void command_prev(void);
void save_last_command(char *input);
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
    if (last_command) {
    first_command = 0;
    char *copy_last_command = strdup(last_command);
    process_commands(copy_last_command);
    free(copy_last_command);
} else {
    printf("No previous command found.\n");
}
}

/**
 * Executes the previous command
 */
void command_prev(void) {
    if (last_command) {
        first_command = 0;  // Prevent welcome message
        char *copy_last_command = strdup(last_command);
        process_commands(copy_last_command);
        free(copy_last_command);
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
 * Executes a pipe between two commands
 */
void execute_pipe(char** args1, char** args2) {
    int pipefd[2];
    pid_t p1, p2;

    if (pipe(pipefd) == -1) {
        perror("pipe failed");
        exit(1);
    }

    p1 = fork();
    if (p1 < 0) {
        perror("fork failed");
        exit(1);
    }

    if (p1 == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        if (execvp(args1[0], args1) == -1) {
            fprintf(stderr, "%s: command not found\n", args1[0]);
            exit(1);
        }
    }

    p2 = fork();
    if (p2 < 0) {
        perror("fork failed");
        exit(1);
    }

    if (p2 == 0) {
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);

        if (execvp(args2[0], args2) == -1) {
            fprintf(stderr, "%s: command not found\n", args2[0]);
            exit(1);
        }
    }

    close(pipefd[0]);
    close(pipefd[1]);

    waitpid(p1, NULL, 0);
    waitpid(p2, NULL, 0);
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
 * Processes and executes commands
 * Handles command sequencing, pipes, and I/O redirection
 */
void process_commands(char* input) {
    save_last_command(input);
    
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

        char* pipe_pos = strchr(command, '|');
        if (pipe_pos) {
            *pipe_pos = '\0';
            char* rhs_command = pipe_pos + 1;

            char** args1 = tokenize(command);
            char** args2 = tokenize(rhs_command);

            if (args1[0] == NULL || args2[0] == NULL) {
                fprintf(stderr, "Invalid pipe command.\n");
            } else {
                execute_pipe(args1, args2);
            }

            for (int i = 0; args1[i] != NULL; i++) free(args1[i]);
            free(args1);

            for (int i = 0; args2[i] != NULL; i++) free(args2[i]);
            free(args2);
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

    if (last_command) free(last_command);
    return 0;
}