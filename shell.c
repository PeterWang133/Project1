#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define INITIAL_TOKEN_SIZE 64
#define INITIAL_INPUT_SIZE 256

char *last_command = NULL;

void process_commands(char* input);

char** allocate_tokens(int size) {
    return (char **)malloc(size * sizeof(char *));
}

char** resize_tokens(char** tokens, int *size) {
    *size *= 2;  // Double the current size
    return (char **)realloc(tokens, (*size) * sizeof(char *));
}

// Tokenizer function
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
            if (!in_quotes) {
                buffer[buffer_index] = '\0';
                tokens[token_count++] = strdup(buffer);
                buffer_index = 0;
                
                if (token_count >= token_size) {
                    tokens = resize_tokens(tokens, &token_size);
                }
            }
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
                // Resize if token array is full
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

// Save the last command
void save_last_command(char *input) {  // Fixed typo
    if (last_command) free(last_command);
    last_command = strdup(input);
}

void command_prev() {
    if (last_command) {
        printf("%s\n", last_command);  // Print the previous command
        // Process and execute the previous command
        char *copy_last_command = strdup(last_command);  // Create a copy to avoid modifying the original
        process_commands(copy_last_command);  // Execute the copied command
        free(copy_last_command);  // Free the copy after execution
    }
}

// Built-in command for 'help'
void command_help() {
    printf("Available built-in commands:\n");
    printf("cd [path] - Change directory\n");
    printf("source [filename] - Execute script\n");
    printf("prev - Repeat previous command\n");
    printf("help - Show this help message\n");
    printf("exit - Exit the shell\n");
}

void command_cd(char **args) {  // Accepts the full array
    if (args[1] == NULL) {
        chdir(getenv("HOME"));
    } else {
        if (chdir(args[1]) != 0) {
            perror("cd");
        }
    }
}

// Execute the command in a specified file
void command_source(char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("source");
        return;
    }
    char line[INITIAL_INPUT_SIZE];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = 0; // Remove newline
        process_commands(line); // Reuse your process_commands function
    }
    fclose(file);
}

void execute_pipe(char** args1, char** args2) {
    int pipefd[2];  // Array to store the pipe's read and write file descriptors
    pid_t p1, p2;

    if (pipe(pipefd) == -1) {
        perror("pipe failed");
        exit(1);
    }

    // First child process for the LHS command
    p1 = fork();
    if (p1 < 0) {
        perror("fork failed");
        exit(1);
    }

    if (p1 == 0) {
        // In the LHS child process: redirect stdout to the write end of the pipe
        close(pipefd[0]);  // Close the unused read end
        dup2(pipefd[1], STDOUT_FILENO);  // Redirect stdout to the pipe's write end
        close(pipefd[1]);  // Close the write end after duplication

        if (execvp(args1[0], args1) == -1) {
            perror("execvp LHS failed");
            exit(1);
        }
    }

    // Second child process for the RHS command
    p2 = fork();
    if (p2 < 0) {
        perror("fork failed");
        exit(1);
    }

    if (p2 == 0) {
        // In the RHS child process: redirect stdin to the read end of the pipe
        close(pipefd[1]);  // Close the unused write end
        dup2(pipefd[0], STDIN_FILENO);  // Redirect stdin to the pipe's read end
        close(pipefd[0]);  // Close the read end after duplication

        if (execvp(args2[0], args2) == -1) {
            perror("execvp RHS failed");
            exit(1);
        }
    }

    // Parent process: close both ends of the pipe and wait for both children
    close(pipefd[0]);
    close(pipefd[1]);

    wait(NULL);  // Wait for the first child (LHS) to finish
    wait(NULL);  // Wait for the second child (RHS) to finish
}

// Functionality of execute_command
void execute_command(char** args, char* input_file, char* output_file) {
   pid_t pid = fork();

    if (pid < 0) {
        perror("Fork Failed");
        exit(1);
    } 
    else if (pid == 0) {
        // Input redirection
        if (input_file) {
            int fd_in = open(input_file, O_RDONLY);
            if (fd_in < 0) {
                perror("open input");
                exit(1);
            }
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }

        // Output redirection
        if (output_file) {
            int fd_out = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);  // Create or truncate the file
            if (fd_out < 0) {
                perror("open output");
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
        wait(NULL);  // Parent process waits for child to complete
    }
}

// Function to handle semicolons and execute each command separately
void process_commands(char* input) {

   if (strcmp(input, "prev") != 0) {
        save_last_command(input);  // Save the last command only if it's not 'prev'
    }

    // Split the input by semicolons
    char* command = strtok(input, ";");
    while (command != NULL) {
        // Trim leading and trailing whitespace from the command
        while (isspace(*command)) command++;
        char* end = command + strlen(command) - 1;
        while (end > command && isspace(*end)) end--;
        *(end + 1) = '\0';

        char *input_file = NULL, *output_file = NULL;
        char *input_redirect_pos = strchr(command, '<');
        char *output_redirect_pos = strchr(command, '>');


        if (input_redirect_pos) {
            *input_redirect_pos = '\0';  // Terminate the command at '<'
            input_file = strtok(input_redirect_pos + 1, " \t");  // Get the file name after '<'
        }

         if (output_redirect_pos) {
            *output_redirect_pos = '\0';  // Terminate the command at '>'
            output_file = strtok(output_redirect_pos + 1, " \t");  // Get the file name after '>'
        }

        char* pipe_pos = strchr(command, '|');
        if (pipe_pos) {
            *pipe_pos = '\0';  // Split the command at the pipe symbol
            char* rhs_command = pipe_pos + 1;  // RHS command is after the pipe

            // Tokenize both LHS and RHS commands
            char** args1 = tokenize(command);
            char** args2 = tokenize(rhs_command);

            // Execute the two commands with piping
            if (args1[0] != NULL && args2[0] != NULL) {
                execute_pipe(args1, args2);
            }

            // Free the tokens
            for (int i = 0; args1[i] != NULL; i++) {
                free(args1[i]);
            }
            free(args1);

            for (int i = 0; args2[i] != NULL; i++) {
                free(args2[i]);
            }
            free(args2);
        }

        else {
        // Tokenize and execute the command
        char** args = tokenize(command);

        if (args[0] != NULL) {
            if (strcmp(args[0], "help") == 0){
                command_help();
            }
            else if (strcmp(args[0], "prev") == 0) {
                command_prev();
            }
            else if (strcmp(args[0], "source") == 0) {
                command_source(args[1]);
            }
            else if (strcmp(args[0], "cd") == 0) {
                command_cd(args);
            }
            else {
            execute_command(args, input_file, output_file);
            }
        }
        
        // Free the tokens
        for (int i = 0; args[i] != NULL; i++) {
            free(args[i]);
        }
        free(args);
        }
        // Get the next command
        command = strtok(NULL, ";");
    }
}

int main(int argc, char **argv) {
    char input[INITIAL_INPUT_SIZE];

    // Welcome message
    printf("Welcome to the mini-shell.\n");
    while (1) {
        printf("shell $ ");
        fflush(stdout);

        // Handle Ctrl-D and if input is 'exit'
        if ((fgets(input, INITIAL_INPUT_SIZE, stdin) == NULL) || (strcmp(input, "exit\n") == 0)) {
            printf("Bye bye.\n");
            break;
        }

        // Remove newline character from input
        input[strcspn(input, "\n")] = 0;

        // Process and execute commands, handling semicolons
        process_commands(input);
    }
    return 0;
}