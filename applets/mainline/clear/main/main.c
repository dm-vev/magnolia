#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *g_version = "Magnolia coreutils 0.1";

static void usage(void)
{
    printf("usage: clear [--help] [--version]\n");
}

static int write_all(int fd, const void *buf, size_t len)
{
    const unsigned char *p = (const unsigned char *)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, p + off, len - off);
        if (w < 0) {
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && argv[1] && strcmp(argv[1], "--help") == 0) {
        usage();
        return 0;
    }
    if (argc == 2 && argv[1] && strcmp(argv[1], "--version") == 0) {
        printf("clear (%s)\n", g_version);
        return 0;
    }

    static const char seq[] = "\x1b[2J\x1b[H";
    if (write_all(STDOUT_FILENO, seq, sizeof(seq) - 1) != 0) {
        fprintf(stderr, "clear: write: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
