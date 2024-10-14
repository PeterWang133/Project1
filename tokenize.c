#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_INPUT_SIZE 256

// Function to tokenize input string
char** tokenize(char* input) {
    char** tokens = malloc(MAX_INPUT_SIZE * sizeof(char*));
    int token_count = 0;
    char buffer[MAX_INPUT_SIZE];
    int buffer_index = 0;
    int in_quotes = 0;
    
    for (int i = 0; input[i] != '\0'; i++) {
        char c = input[i];
        
        // Handle quoted strings
        if (c == '"') {
            in_quotes = !in_quotes;
            if (!in_quotes) {
                buffer[buffer_index] = '\0';
                tokens[token_count++] = strdup(buffer);
                buffer_index = 0;
            }
            continue;
        }
        
        // If inside quotes, add everything to the buffer
        if (in_quotes) {
            buffer[buffer_index++] = c;
            continue;
        }
        
        // Handle special tokens
        if (strchr("()<>|;", c)) {
            if (buffer_index > 0) {
                buffer[buffer_index] = '\0';
                tokens[token_count++] = strdup(buffer);
                buffer_index = 0;
            }
            char special_token[2] = {c, '\0'};
            tokens[token_count++] = strdup(special_token);
            continue;
        }
        
        // Handle whitespace
        if (isspace(c)) {
            if (buffer_index > 0) {
                buffer[buffer_index] = '\0';
                tokens[token_count++] = strdup(buffer);
                buffer_index = 0;
            }
            continue;
        }
        
        // Accumulate normal characters into the buffer
        buffer[buffer_index++] = c;
    }
    
    // Add any remaining buffer content as the last token
    if (buffer_index > 0) {
        buffer[buffer_index] = '\0';
        tokens[token_count++] = strdup(buffer);
    }
    
    tokens[token_count] = NULL; // Null-terminate the list of tokens
    return tokens;
}

// Updated main function to handle command-line arguments
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_string>\n", argv[0]);
        return 1;
    }

    char *input = argv[1];

    // Tokenize the input string
    char **tokens = tokenize(input);

    // Print out each token
    for (int i = 0; tokens[i] != NULL; i++) {
        printf("%s\n", tokens[i]);
        free(tokens[i]); // Free allocated memory for each token
    }

    free(tokens); // Free the token array
    return 0;
}
