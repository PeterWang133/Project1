#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Constants */
#define MAX_INPUT_SIZE 256
#define INITIAL_TOKEN_SIZE 64

/**
 * Structure to hold tokenizer state
 * This helps manage the tokenization process and memory
 */
typedef struct {
    char **tokens;        // Array of tokens
    int token_count;      // Number of tokens
    int capacity;         // Current capacity of tokens array
} Tokenizer;

/**
 * Initialize a new tokenizer
 * @return Initialized tokenizer or NULL if allocation fails
 */
Tokenizer* init_tokenizer() {
    Tokenizer *t = malloc(sizeof(Tokenizer));
    if (!t) return NULL;
    
    t->capacity = INITIAL_TOKEN_SIZE;
    t->tokens = malloc(sizeof(char*) * t->capacity);
    if (!t->tokens) {
        free(t);
        return NULL;
    }
    t->token_count = 0;
    return t;
}

/**
 * Add a token to the tokenizer
 * @param t Tokenizer instance
 * @param token String to add as token
 * @return 0 on success, -1 on failure
 */
int add_token(Tokenizer *t, const char *token) {
    // Skip empty tokens
    if (!token || strlen(token) == 0) return 0;
    
    // Resize if necessary
    if (t->token_count >= t->capacity) {
        t->capacity *= 2;
        char **new_tokens = realloc(t->tokens, sizeof(char*) * t->capacity);
        if (!new_tokens) return -1;
        t->tokens = new_tokens;
    }
    
    // Copy token
    t->tokens[t->token_count] = strdup(token);
    if (!t->tokens[t->token_count]) return -1;
    t->token_count++;
    return 0;
}

/**
 * Check if a character is a special shell character
 * @param c Character to check
 * @return 1 if special, 0 if not
 */
int is_special(char c) {
    return (c == '(' || c == ')' || c == '<' || c == '>' || 
            c == ';' || c == '|');
}

/**
 * Tokenize input string into shell tokens
 * @param input Input string to tokenize
 * @return Tokenizer containing the tokens, or NULL on failure
 */
Tokenizer* tokenize(const char *input) {
    if (!input) return NULL;
    
    Tokenizer *t = init_tokenizer();
    if (!t) return NULL;
    
    char buffer[MAX_INPUT_SIZE] = {0};
    int buf_pos = 0;
    int in_quotes = 0;
    
    for (int i = 0; input[i] != '\0'; i++) {
        char c = input[i];
        
        // Handle quotes
        if (c == '"') {
            if (!in_quotes) {
                // Start quoted section
                if (buf_pos > 0) {
                    // Add accumulated token before quote
                    buffer[buf_pos] = '\0';
                    add_token(t, buffer);
                    buf_pos = 0;
                }
                in_quotes = 1;
            } else {
                // End quoted section
                buffer[buf_pos] = '\0';
                add_token(t, buffer);
                buf_pos = 0;
                in_quotes = 0;
            }
            continue;
        }
        
        if (in_quotes) {
            buffer[buf_pos++] = c;
            continue;
        }
        
        // Handle special characters
        if (is_special(c)) {
            if (buf_pos > 0) {
                // Add accumulated token
                buffer[buf_pos] = '\0';
                add_token(t, buffer);
                buf_pos = 0;
            }
            // Add special character as token
            buffer[0] = c;
            buffer[1] = '\0';
            add_token(t, buffer);
            continue;
        }
        
        // Handle whitespace
        if (isspace(c)) {
            if (buf_pos > 0) {
                buffer[buf_pos] = '\0';
                add_token(t, buffer);
                buf_pos = 0;
            }
            continue;
        }
        
        // Add to current token
        buffer[buf_pos++] = c;
    }
    
    // Add final token if any
    if (buf_pos > 0) {
        buffer[buf_pos] = '\0';
        add_token(t, buffer);
    }
    
    return t;
}

/**
 * Free tokenizer and all associated memory
 * @param t Tokenizer to free
 */
void free_tokenizer(Tokenizer *t) {
    if (!t) return;
    for (int i = 0; i < t->token_count; i++) {
        free(t->tokens[i]);
    }
    free(t->tokens);
    free(t);
}

/**
 * Read a line from stdin
 * @return Dynamically allocated string containing the line, or NULL on error
 */
char* read_input() {
    char buffer[MAX_INPUT_SIZE];
    char *result = NULL;
    size_t total_size = 0;
    
    while (fgets(buffer, sizeof(buffer), stdin)) {
        size_t len = strlen(buffer);
        
        // Remove trailing newline if present
        if (len > 0 && buffer[len-1] == '\n') {
            buffer[--len] = '\0';
        }
        
        // Allocate/reallocate result buffer
        char *new_result = realloc(result, total_size + len + 1);
        if (!new_result) {
            free(result);
            return NULL;
        }
        result = new_result;
        
        // Copy new data
        strcpy(result + total_size, buffer);
        total_size += len;
        
        // If we didn't fill the buffer, we're done
        if (len < sizeof(buffer) - 1) break;
    }
    
    return result;
}

// main function
int main() {
    // Read input
    char *input = read_input();
    if (!input) {
        fprintf(stderr, "Failed to read input\n");
        return 1;
    }
    
    // Tokenize input
    Tokenizer *t = tokenize(input);
    if (!t) {
        fprintf(stderr, "Failed to tokenize input\n");
        free(input);
        return 1;
    }
    
    // Print tokens
    for (int i = 0; i < t->token_count; i++) {
        printf("%s\n", t->tokens[i]);
    }
    
    // Cleanup
    free(input);
    free_tokenizer(t);
    return 0;
}

