#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *g_version = "Magnolia coreutils 0.1";

static void eprintf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) {
        return;
    }
    size_t len = (size_t)n;
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1;
    }
    (void)write(STDERR_FILENO, buf, len);
}

static void usage(void)
{
    eprintf("usage: clear [-T term] [-V] [-x]\n");
}

static int write_all(int fd, const void *buf, size_t len)
{
    const unsigned char *p = (const unsigned char *)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, p + off, len - off);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (w == 0) {
            errno = EIO;
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

struct clear_term {
    const char *name;
    const char *clear_screen;
    const char *clear_scrollback;
};

static const struct clear_term g_terms[] = {
    { "xterm", "\x1b[H\x1b[2J", "\x1b[3J" },
    { "xterm-256color", "\x1b[H\x1b[2J", "\x1b[3J" },
    { "xterm-color", "\x1b[H\x1b[2J", "\x1b[3J" },
    { "screen", "\x1b[H\x1b[2J", "\x1b[3J" },
    { "screen-256color", "\x1b[H\x1b[2J", "\x1b[3J" },
    { "tmux", "\x1b[H\x1b[2J", "\x1b[3J" },
    { "tmux-256color", "\x1b[H\x1b[2J", "\x1b[3J" },
    { "vt100", "\x1b[H\x1b[2J", NULL },
    { "ansi", "\x1b[H\x1b[2J", NULL },
    { "linux", "\x1b[H\x1b[2J", NULL },
};

static const struct clear_term *lookup_term(const char *term)
{
    if (!term || term[0] == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(g_terms) / sizeof(g_terms[0]); ++i) {
        if (strcmp(term, g_terms[i].name) == 0) {
            return &g_terms[i];
        }
    }
    return NULL;
}

static int emit_clear(const struct clear_term *term, bool clear_scrollback)
{
    if (!term || !term->clear_screen) {
        errno = EINVAL;
        return -1;
    }
    if (write_all(STDOUT_FILENO, term->clear_screen, strlen(term->clear_screen)) != 0) {
        return -1;
    }
    if (clear_scrollback && term->clear_scrollback) {
        if (write_all(STDOUT_FILENO, term->clear_scrollback, strlen(term->clear_scrollback)) != 0) {
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        }
    }

    opterr = 0;
    const char *term_override = NULL;
    bool no_scrollback = false;
    bool show_version = false;

    int opt;
    while ((opt = getopt(argc, argv, ":T:Vx")) != -1) {
        switch (opt) {
        case 'T':
            term_override = optarg;
            break;
        case 'V':
            show_version = true;
            break;
        case 'x':
            no_scrollback = true;
            break;
        case ':':
            eprintf("clear: option requires an argument -- %c\n", optopt);
            usage();
            return 1;
        default:
            eprintf("clear: illegal option -- %c\n", optopt);
            usage();
            return 1;
        }
    }

    if (optind < argc) {
        usage();
        return 1;
    }

    if (show_version) {
        printf("clear (%s)\n", g_version);
        return 0;
    }

    const char *term = term_override ? term_override : getenv("TERM");
    if (!term || term[0] == '\0') {
        eprintf("clear: TERM environment variable not set.\n");
        return 1;
    }

    /* TODO: Replace the fixed table with real terminfo data when available. */
    const struct clear_term *cap = lookup_term(term);
    if (!cap) {
        eprintf("clear: unknown terminal type %s\n", term);
        return 1;
    }

    if (emit_clear(cap, !no_scrollback) != 0) {
        eprintf("clear: stdout: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
