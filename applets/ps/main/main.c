#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    char cwd[256];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        snprintf(cwd, sizeof(cwd), "<cwd error: %s>", strerror(errno));
    }

    printf("PID\tPPID\tCWD\n");
    printf("%d\t%d\t%s\n", getpid(), getppid(), cwd);
    return 0;
}

