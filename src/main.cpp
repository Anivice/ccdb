#include <thread>
#include "readline.h"
#include "history.h"

int main()
{
    rl_variable_bind("search-ignore-case", "on");

    char *line;
    while ((line = readline("myapp> ")) != nullptr) {
        /* Add non‑empty lines to the history so they can be recalled with ↑/↓. */
        if (*line) {
            add_history(line);
        }

        /* Process the input: here we simply echo it back. */
        printf("read: %s\n", line);

        /* Exit on the word "exit". */
        if (strcmp(line, "exit") == 0) {
            free(line);
            break;
        }
        free(line);
    }
    return 0;
}
