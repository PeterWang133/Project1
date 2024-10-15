#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>

#define INITIAL_TOKEN_SIZE 64
#define INITIAL_INPUT_SIZE 256

// strdup function implementation
char* strdup(const char* str) {
    size_t len = strlen(str) + 1;
    char* copy = malloc(len);
    if (copy) {
        memcpy(copy, str, len);
    }
    return copy;
}

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

// Functionality of execute_command
void execute_command(char** args) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("Fork Failed");
        exit(1);
    } 
    else if (pid == 0) {
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
    // Split the input by semicolons
    char* command = strtok(input, ";");
    while (command != NULL) {
        // Trim leading and trailing whitespace from the command
        while (isspace(*command)) command++;
        char* end = command + strlen(command) - 1;
        while (end > command && isspace(*end)) end--;
        *(end + 1) = '\0';

        // Tokenize and execute the command
        char** args = tokenize(command);
        if (args[0] != NULL) {
            execute_command(args);
        }
        
        // Free the tokens
        for (int i = 0; args[i] != NULL; i++) {
            free(args[i]);
        }
        free(args);

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