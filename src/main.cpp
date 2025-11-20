#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "readline.h"
#include "history.h"

/* List of words we want to complete.  The final NULL terminates the list. */
static const char *commands[] = {
    "help", "list", "delete", "quit", "rename", "view", nullptr
};

/* Generator function for rl_completion_matches.  It returns one match per call. */
static char *command_generator(const char *text, int state) {
    static size_t list_index, len;

    if (state == 0) {
        list_index = 0;
        len = strlen(text);
    }

    /* Return the next command that matches TEXT. */
    while (commands[list_index]) {
        const char *name = commands[list_index++];
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }
    return nullptr;  /* No more matches. */
}

/* Completion function called by Readline when TAB is pressed. */
static char **command_completion(const char *text, int start, int end) {
    (void)end;  /* Unused parameter. */
    char **matches = nullptr;

    /* If completing the first word, use our command list.  Otherwise use filename completion. */
    if (start == 0) {
        /* Tell Readline not to perform default filename completion when no matches are found. */
        rl_attempted_completion_over = 1;
        matches = rl_completion_matches(text, command_generator);
    }
    return matches;
}

int main(void) {
    rl_attempted_completion_function = command_completion; /* Register our completer. */

    char *line;
    while ((line = readline("myapp> ")) != nullptr) {
        if (*line) {
            add_history(line);  /* Keep non‑empty lines in history. */
        }
        printf("read: %s\n", line);
        if (strcmp(line, "quit") == 0) {
            free(line);
            break;
        }
        free(line);
    }
    return 0;
}
