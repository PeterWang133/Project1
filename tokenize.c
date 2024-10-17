#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>

#define INITIAL_TOKEN_SIZE 64
#define INITIAL_INPUT_SIZE 256

// Purpose: Allocates memory for tokens
char** allocate_tokens(int size) {
    return (char **)malloc(size * sizeof(char *));
}

// Purpose: Resizes token array when it exceeds initial size
char** resize_tokens(char** tokens, int *size) {
    *size *= 2;  // Double the current size
    return (char **)realloc(tokens, (*size) * sizeof(char *));
}

// Purpose: Tokenizer function to split input into command tokens
char** tokenize(char* input) {
    int token_size = INITIAL_TOKEN_SIZE;
    char** tokens = allocate_tokens(token_size);
    int token_count = 0;
    
    int in_quotes = 0;
    char buffer[INITIAL_INPUT_SIZE];
    int buffer_index = 0;
    
    for (int i = 0; input[i] != '\0'; i++) {
        char c = input[i];
      
        // Handle quotes
        if (c == '"') {
            in_quotes = !in_quotes;
            continue;
        }
        
        // If inside quotes, append characters to the buffer
        if (in_quotes) {
            buffer[buffer_index++] = c;
            continue;
        }
        
        // Handle spaces as delimiters between tokens
        if (isspace(c)) {
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
        
        // Handle shell special characters (e.g., pipes, redirects)
        if (strchr("|<>", c)) {
            if (buffer_index > 0) {
                buffer[buffer_index] = '\0';
                tokens[token_count++] = strdup(buffer);
                buffer_index = 0;
                
                if (token_count >= token_size) {
                    tokens = resize_tokens(tokens, &token_size);
                }
            }
            // Special character as a separate token
            char special_token[2] = {c, '\0'};
            tokens[token_count++] = strdup(special_token);
            
            if (token_count >= token_size) {
                tokens = resize_tokens(tokens, &token_size);
            }
            continue;
        }
        
        // Add regular characters to the buffer
        buffer[buffer_index++] = c;
    }
    
    // Finalize last token in the buffer
    if (buffer_index > 0) {
        buffer[buffer_index] = '\0';
        tokens[token_count++] = strdup(buffer);
        if (token_count >= token_size) {
            tokens = resize_tokens(tokens, &token_size);
        }
    }
    
    tokens[token_count] = NULL;  // Null-terminate the token array
    return tokens;
}

// Purpose: Executes a single command using fork and execvp
void execute_command(char** args) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("Fork Failed");
        exit(1);
    } 
    else if (pid == 0) {
        // In child process, execute the command
        if (execvp(args[0], args) == -1) {
            fprintf(stderr, "%s: command not found\n", args[0]);
            exit(1);
        }
    } else {
        // In parent process, wait for the child to complete
        wait(NULL);
    }
}

// Purpose: Process commands and handle multiple commands split by semicolons
void process_commands(char* input) {
    int input_length = strlen(input);
    int start = 0;

    // Process each command separately
    for (int i = 0; i <= input_length; i++) {
        if (input[i] == ';' || input[i] == '\0') {
            input[i] = '\0';  // Temporarily terminate the command

            // Tokenize and execute the current command
            char** args = tokenize(&input[start]);
            if (args[0] != NULL) {
                execute_command(args);
            }

            // Free the tokens
            for (int j = 0; args[j] != NULL; j++) {
                free(args[j]);
            }
            free(args);

            start = i + 1;  // Move to the next command after the semicolon
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_string>\n", argv[0]);
        return 1;
    }

    char *input = argv[1];

    char **tokens = tokenize(input);

    // free memory space after printing out the tokens
    for (int i = 0; tokens[i] != NULL; i++) {
        printf("%s\n", tokens[i]);
        free(tokens[i]);
    }

    free(tokens);
    return 0;
}
