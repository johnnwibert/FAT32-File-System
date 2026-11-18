#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *resize(void *ptr, size_t bytes) {
    void *result = realloc(ptr, bytes);
    if (!result) {
        perror("lexer allocation");
        exit(EXIT_FAILURE);
    }
    return result;
}

char *get_input(void) {
    size_t capacity = 64, used = 0;
    char *line = resize(NULL, capacity);
    int ch;
    while ((ch = getchar()) != EOF && ch != '\n') {
        if (used == capacity - 1) {
            capacity *= 2;
            line = resize(line, capacity);
        }
        line[used++] = (char)ch;
    }
    if (ch == EOF && used == 0) {
        free(line);
        return NULL;
    }
    line[used] = 0;
    return line;
}

tokenlist *new_tokenlist(void) {
	tokenlist *tokens = resize(NULL, sizeof(tokenlist));
	tokens->size = 0;
	tokens->items = resize(NULL, sizeof(char *));
	tokens->items[0] = NULL; /* make NULL terminated */
	return tokens;
}

void add_token(tokenlist *tokens, char *item) {
	size_t i = tokens->size;

	tokens->items = resize(tokens->items, (i + 2) * sizeof(char *));
	tokens->items[i] = resize(NULL, strlen(item) + 1);
	tokens->items[i + 1] = NULL;
	strcpy(tokens->items[i], item);

	tokens->size += 1;
}

tokenlist *get_tokens(char *input) {
	char *buf = resize(NULL, strlen(input) + 1);
	strcpy(buf, input);
	tokenlist *tokens = new_tokenlist();
	char *tok = strtok(buf, " \t\r\n");
	while (tok != NULL)
	{
		add_token(tokens, tok);
		tok = strtok(NULL, " \t\r\n");
	}
	free(buf);
	return tokens;
}

void free_tokens(tokenlist *tokens) {
	for (size_t i = 0; i < tokens->size; i++)
		free(tokens->items[i]);
	free(tokens->items);
	free(tokens);
}
