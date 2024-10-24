#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

// Constants
#define INITIAL_TOKEN_SIZE 64
#define INITIAL_INPUT_SIZE 256
#define MAX_PATH_LENGTH 4096

// Global variables
char *last_command = NULL;
int in_prev_command = 0;

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
    char** tokens = (char **)malloc(size * sizeof(char *));
    if (!tokens) {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }
    return tokens;
}

/**
 * Resizes token array when needed
 */
char** resize_tokens(char** tokens, int *size) {
    *size *= 2;
    char** new_tokens = (char **)realloc(tokens, (*size) * sizeof(char *));
    if (!new_tokens) {
        perror("Memory reallocation failed");
        free(tokens);
        exit(EXIT_FAILURE);
    }
    return new_tokens;
}

/**
 * Tokenizes input string into array of strings
 */
char** tokenize(char* input) {
    int token_size = INITIAL_TOKEN_SIZE;
    char** tokens = allocate_tokens(token_size);
    int token_count = 0;
    
    int in_quotes = 0;
    char buffer[INITIAL_INPUT_SIZE] = {0};
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
                
                if (token_count >= token_size - 1) {
                    tokens = resize_tokens(tokens, &token_size);
                }
            }
            if (!isspace(c)) {
                char special_token[2] = {c, '\0'};
                tokens[token_count++] = strdup(special_token);
                
                if (token_count >= token_size - 1) {
                    tokens = resize_tokens(tokens, &token_size);
                }
            }
            continue;
        }
        
        if (isspace(c)) {
            if (buffer_index > 0) {
                buffer[buffer_index] = '\0';
                tokens[token_count++] = strdup(buffer);
                buffer_index = 0;
                
                if (token_count >= token_size - 1) {
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
    }
    
    tokens[token_count] = NULL;
    return tokens;
}

/**
 * Saves the last executed command
 */
void save_last_command(char *input) {
    if (input && strlen(input) > 0 && !in_prev_command) {
        cleanup_last_command();
        last_command = strdup(input);
    }
}

/**
 * Executes the previous command
 */
void command_prev(void) {
    if (last_command && strlen(last_command) > 0) {
        printf("%s\n", last_command);  // Echo the command being executed
        char *command_copy = strdup(last_command);
        if (!command_copy) {
            perror("Memory allocation failed");
            exit(EXIT_FAILURE);
        }
        in_prev_command = 1;
        process_commands(command_copy);
        in_prev_command = 0;
        free(command_copy);
    } else {
        printf("No previous command found.\n");
    }
}

/**
 * Frees the memory allocated for the last_command variable
 */
void cleanup_last_command(void) {
    free(last_command);
    last_command = NULL;
}

/**
 * Displays help information
 */
void command_help(void) {
    printf("Available built-in commands:\n");
    printf("cd [path] - Change directory\n");
    printf("source [filename] - Execute commands from file\n");
    printf("prev - Repeat previous command\n");
    printf("help - Show this help message\n");
    printf("exit - Exit the shell\n");
}

/**
 * Changes current directory
 */
void command_cd(char **args) {
    char *path = args[1];
    if (!path) {
        path = getenv("HOME");
    }
    
    if (chdir(path) != 0) {
        perror("cd failed");
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
        perror("source failed");
        return;
    }
    
    char line[INITIAL_INPUT_SIZE];
    while (fgets(line, sizeof(line), file)) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }
        if (strlen(line) > 0) {
            process_commands(line);
        }
    }
    
    fclose(file);
}

/**
 * Executes a piped command sequence
 */
void execute_pipe(char*** commands, int num_commands, char* input_file, char* output_file) {
    int pipes[num_commands - 1][2];
    pid_t pids[num_commands];
    
    // Create pipes
    for (int i = 0; i < num_commands - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe failed");
            exit(EXIT_FAILURE);
        }
    }
    
    // Execute commands
    for (int i = 0; i < num_commands; i++) {
        pids[i] = fork();
        
        if (pids[i] == -1) {
            perror("fork failed");
            exit(EXIT_FAILURE);
        }
        
        if (pids[i] == 0) {  // Child process
            // Handle input redirection for first command
            if (i == 0 && input_file) {
                int fd = open(input_file, O_RDONLY);
                if (fd == -1) {
                    perror("open input failed");
                    exit(EXIT_FAILURE);
                }
                if (dup2(fd, STDIN_FILENO) == -1) {
                    perror("dup2 input failed");
                    exit(EXIT_FAILURE);
                }
                close(fd);
            }
            
            // Handle output redirection for last command
            if (i == num_commands - 1 && output_file) {
                int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd == -1) {
                    perror("open output failed");
                    exit(EXIT_FAILURE);
                }
                if (dup2(fd, STDOUT_FILENO) == -1) {
                    perror("dup2 output failed");
                    exit(EXIT_FAILURE);
                }
                close(fd);
            }
            
            // Connect pipes
            if (i > 0) {  // Not first command
                if (dup2(pipes[i-1][0], STDIN_FILENO) == -1) {
                    perror("dup2 pipe input failed");
                    exit(EXIT_FAILURE);
                }
            }
            if (i < num_commands - 1) {  // Not last command
                if (dup2(pipes[i][1], STDOUT_FILENO) == -1) {
                    perror("dup2 pipe output failed");
                    exit(EXIT_FAILURE);
                }
            }
            
            // Close all pipe fds
            for (int j = 0; j < num_commands - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            
            execvp(commands[i][0], commands[i]);
            perror("execvp failed");
            exit(EXIT_FAILURE);
        }
    }
    
    // Parent closes all pipe fds
    for (int i = 0; i < num_commands - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    
    // Wait for all children
    for (int i = 0; i < num_commands; i++) {
        int status;
        waitpid(pids[i], &status, 0);
    }
}

/**
 * Executes a single command with I/O redirection
 */
void execute_command(char** args, char* input_file, char* output_file) {
    pid_t pid = fork();
    
    if (pid == -1) {
        perror("fork failed");
        exit(EXIT_FAILURE);
    }
    
    if (pid == 0) {  // Child process
        // Handle input redirection
        if (input_file) {
            int fd = open(input_file, O_RDONLY);
            if (fd == -1) {
                perror("open input failed");
                exit(EXIT_FAILURE);
            }
            if (dup2(fd, STDIN_FILENO) == -1) {
                perror("dup2 input failed");
                exit(EXIT_FAILURE);
            }
            close(fd);
        }
        
        // Handle output redirection
        if (output_file) {
            int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd == -1) {
                perror("open output failed");
                exit(EXIT_FAILURE);
            }
            if (dup2(fd, STDOUT_FILENO) == -1) {
                perror("dup2 output failed");
                exit(EXIT_FAILURE);
            }
            close(fd);
        }
        
        execvp(args[0], args);
        perror("execvp failed");
        exit(EXIT_FAILURE);
    }
    
    // Parent waits for child
    int status;
    waitpid(pid, &status, 0);
}

/**
 * Process and execute commands
 */
void process_commands(char* input) {
    if (!input || strlen(input) == 0) return;
    
    // Save command history if not in prev command
    if (!in_prev_command) {
        save_last_command(input);
    }
    
    char* saveptr1;
    char* command = strtok_r(input, ";", &saveptr1);
    
    while (command) {
        // Trim whitespace
        while (isspace(*command)) command++;
        char* end = command + strlen(command) - 1;
        while (end > command && isspace(*end)) end--;
        *(end + 1) = '\0';
        
        if (strlen(command) == 0) {
            command = strtok_r(NULL, ";", &saveptr1);
            continue;
        }
        
        char *input_file = NULL, *output_file = NULL;
        char *input_redirect_pos = strstr(command, "<");
        char *output_redirect_pos = strstr(command, ">");
        
        // Handle input redirection
        if (input_redirect_pos) {
            *input_redirect_pos = '\0';
            input_redirect_pos++;
            while (isspace(*input_redirect_pos)) input_redirect_pos++;
            char* end = input_redirect_pos;
            while (*end && !isspace(*end) && *end != '>' && *end != '<' && *end != '|') end++;
            *end = '\0';
            input_file = input_redirect_pos;
        }
        
        // Handle output redirection
        if (output_redirect_pos) {
            *output_redirect_pos = '\0';
            output_redirect_pos++;
            while (isspace(*output_redirect_pos)) output_redirect_pos++;
            char* end = output_redirect_pos;
            while (*end && !isspace(*end) && *end != '>' && *end != '<' && *end != '|') end++;
            *end = '\0';
            output_file = output_redirect_pos;
        }
        
        // Count pipes
        int num_pipes = 0;
        for (char* p = command; *p; p++) {
            if (*p == '|') num_pipes++;
        }
        
        if (num_pipes > 0) {
            // Handle piped commands
            char** pipe_commands[num_pipes + 1];
            char* saveptr2;
            char* pipe_command = strtok_r(command, "|", &saveptr2);
            int index = 0;
            
            while (pipe_command && index <= num_pipes) {
                while (isspace(*pipe_command)) pipe_command++;
                char* end = pipe_command + strlen(pipe_command) - 1;
                while (end > pipe_command && isspace(*end)) end--;
                *(end + 1) = '\0';
                
                pipe_commands[index++] = tokenize(pipe_command);
                pipe_command = strtok_r(NULL, "|", &saveptr2);
            }
            
            execute_pipe(pipe_commands, num_pipes + 1, input_file, output_file);
            
            // Cleanup
            for (int i = 0; i < num_pipes + 1; i++) {
                for (char** arg = pipe_commands[i]; *arg; arg++) {
                    free(*arg);
                }
                free(pipe_commands[i]);
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
        process_commands(input);
    }

    cleanup_last_command();
    return 0;
}