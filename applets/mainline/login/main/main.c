#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>

int m_elf_run_file(const char *path, int argc, char *argv[], int *out_rc);

static void console_puts(const char *s)
{
    if (s == NULL) {
        return;
    }
    (void)write(1, s, strlen(s));
}

static ssize_t console_getline(char *buf, size_t cap)
{
    if (buf == NULL || cap == 0) {
        return -1;
    }

    size_t len = 0;
    while (1) {
        char c = 0;
        ssize_t r = read(0, &c, 1);
        if (r <= 0) {
            return -1;
        }

        if (c == '\r') {
            c = '\n';
        }

        if (c == '\n') {
            console_puts("\n");
            break;
        }

        if (c == '\b' || (unsigned char)c == 0x7f) {
            if (len > 0) {
                len--;
                console_puts("\b \b");
            }
            continue;
        }

        if ((unsigned char)c < 0x20) {
            continue;
        }

        if (len + 1 < cap) {
            buf[len++] = c;
            (void)write(1, &c, 1);
        }
    }

    buf[len] = '\0';
    return (ssize_t)len;
}

static void sanitize_user(char *buf)
{
    if (buf == NULL) {
        return;
    }

    size_t out = 0;
    for (size_t i = 0; buf[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)buf[i];
        if (isalnum(c)) {
            buf[out++] = (char)c;
        }
    }
    buf[out] = '\0';
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    char user[32];
    static char sh_argv0[] = "sh";
    static char sh_path[] = "/bin/sh";
    char *sh_argv[] = { sh_argv0, NULL };

    while (1) {
        console_puts("login: ");
        if (console_getline(user, sizeof(user)) < 0) {
            sleep(1);
            continue;
        }

        sanitize_user(user);

        bool blank = true;
        for (size_t i = 0; user[i] != '\0'; ++i) {
            if (!isspace((unsigned char)user[i])) {
                blank = false;
                break;
            }
        }
        if (blank) {
            continue;
        }

        if (strcmp(user, "root") != 0) {
            console_puts("login incorrect\n");
            continue;
        }

        console_puts("login: starting /bin/sh\n");
        int rc = 0;
        int ret = m_elf_run_file(sh_path, 1, sh_argv, &rc);
        if (ret != 0) {
            printf("login: m_elf_run_file(%s) failed ret=%d\n", sh_path, ret);
            sleep(1);
            continue;
        }
        printf("login: /bin/sh exited rc=%d\n", rc);
        (void)rc;
    }
}
