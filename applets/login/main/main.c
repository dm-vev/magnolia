#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

        if (len + 1 < cap) {
            buf[len++] = c;
            (void)write(1, &c, 1);
        }
    }

    buf[len] = '\0';
    return (ssize_t)len;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    char user[32];
    char *sh_argv[] = { (char *)"sh", NULL };

    while (1) {
        console_puts("login: ");
        if (console_getline(user, sizeof(user)) < 0) {
            sleep(1);
            continue;
        }

        if (strcmp(user, "root") != 0) {
            console_puts("login incorrect\n");
            continue;
        }

        int rc = 0;
        int ret = m_elf_run_file("/bin/sh", 1, sh_argv, &rc);
        if (ret != 0) {
            printf("login: m_elf_run_file(/bin/sh) failed ret=%d\n", ret);
            sleep(1);
            continue;
        }
        (void)rc;
    }
}

