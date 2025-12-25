#include <stdio.h>
#include <string.h>

static const char *g_version = "Magnolia coreutils 0.1";

static int wants_help(int argc, char **argv)
{
    return argc == 2 && argv[1] && strcmp(argv[1], "--help") == 0;
}

static int wants_version(int argc, char **argv)
{
    return argc == 2 && argv[1] && strcmp(argv[1], "--version") == 0;
}

int main(int argc, char **argv)
{
    if (wants_help(argc, argv)) {
        printf("usage: true [--help] [--version]\n");
        return 0;
    }
    if (wants_version(argc, argv)) {
        printf("true (%s)\n", g_version);
        return 0;
    }
    (void)argc;
    (void)argv;
    return 0;
}

