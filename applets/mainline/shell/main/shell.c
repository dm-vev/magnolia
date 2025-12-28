#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void prompt(void)
{
    printf("shell> ");
    fflush(stdout);
}

static void handle_line(char *line)
{
    // trim trailing newline
    size_t len = strlen(line);
    if (len && line[len - 1] == '\n') {
        line[len - 1] = '\0';
    }

    if (strlen(line) == 0) {
        return;
    }

    if (strcmp(line, "help") == 0) {
        printf("Commands: help, echo <text>, exit\n");
    } else if (strncmp(line, "echo ", 5) == 0) {
        printf("%s\n", line + 5);
    } else if (strcmp(line, "exit") == 0) {
        printf("bye\n");
        exit(0);
    } else {
        printf("unknown command: %s\n", line);
    }
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    char line[128];

    printf("ESP shell applet ready\n");
    while (fgets(line, sizeof(line), stdin)) {
        handle_line(line);
        prompt();
    }
    return 0;
}
