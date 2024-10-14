#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define INITIAL_TOKEN_SIZE 64
#define INITIAL_INPUT_SIZE 256

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
