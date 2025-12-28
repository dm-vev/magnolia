#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
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

static bool is_shell_space(unsigned char c)
{
    return c == ' ' || c == 0xA0 || c == '\t' || c == '\n' || c == '\r' || c == '\v'
           || c == '\f';
}

static int split_args(char *line, char **argv, int cap)
{
    if (line == NULL || argv == NULL || cap <= 0) {
        return 0;
    }

    int argc = 0;
    char *p = line;
    while (*p != '\0') {
        while (*p != '\0' && is_shell_space((unsigned char)*p)) {
            ++p;
        }
        if (*p == '\0') {
            break;
        }
        if (argc + 1 >= cap) {
            break;
        }
        argv[argc++] = p;
        while (*p != '\0' && !is_shell_space((unsigned char)*p)) {
            ++p;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
    }
    argv[argc] = NULL;
    return argc;
}

static const char *resolve_cmd_path(const char *cmd, char *scratch, size_t cap)
{
    if (cmd == NULL || cmd[0] == '\0') {
        return NULL;
    }
    if (strchr(cmd, '/') != NULL) {
        return cmd;
    }
    if (scratch == NULL || cap == 0) {
        return NULL;
    }
    int n = snprintf(scratch, cap, "/bin/%s", cmd);
    if (n < 0 || (size_t)n >= cap) {
        return NULL;
    }
    return scratch;
}

static int run_external(int argc, char **argv)
{
    if (argc <= 0 || argv == NULL || argv[0] == NULL) {
        return 0;
    }

    char path[96];
    const char *resolved = resolve_cmd_path(argv[0], path, sizeof(path));
    if (resolved == NULL) {
        return 1;
    }

    int rc = 0;
    int ret = m_elf_run_file(resolved, argc, argv, &rc);
    if (ret != 0) {
        if (ret < 0) {
            int err = -ret;
            if (err == ENOENT) {
                printf("%s: not found\n", argv[0]);
            } else {
                printf("%s: %s (ret=%d)\n", argv[0], strerror(err), ret);
            }
        } else {
            printf("%s: failed (ret=%d)\n", argv[0], ret);
        }
        return 127;
    }
    return rc;
}

static int apply_stdout_redirection(int *argc, char **argv, int *saved_stdout)
{
    if (argc == NULL || argv == NULL || saved_stdout == NULL) {
        return -1;
    }

    for (int i = 0; i < *argc; ++i) {
        bool truncate = (strcmp(argv[i], ">") == 0);
        bool append = (strcmp(argv[i], ">>") == 0);
        if (!truncate && !append) {
            continue;
        }

        if (i + 1 >= *argc) {
            printf("sh: syntax error near unexpected token `%s`\n", argv[i]);
            return -1;
        }

        const char *path = argv[i + 1];
        int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
        int fd = open(path, flags, 0644);
        if (fd < 0) {
            printf("sh: %s: %s\n", path, strerror(errno));
            return -1;
        }

        int out = dup(1);
        if (out < 0) {
            close(fd);
            printf("sh: dup: %s\n", strerror(errno));
            return -1;
        }

        if (dup2(fd, 1) < 0) {
            close(fd);
            close(out);
            printf("sh: dup2: %s\n", strerror(errno));
            return -1;
        }
        close(fd);

        for (int j = i; j + 2 < *argc; ++j) {
            argv[j] = argv[j + 2];
        }
        *argc -= 2;
        argv[*argc] = NULL;

        *saved_stdout = out;
        return 0;
    }

    *saved_stdout = -1;
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    char line[256];
    char *args[16];

    while (1) {
        console_puts("# ");
        if (console_getline(line, sizeof(line)) < 0) {
            sleep(1);
            continue;
        }

        int n = split_args(line, args, (int)(sizeof(args) / sizeof(args[0])));
        if (n == 0) {
            continue;
        }

        if (strcmp(args[0], "exit") == 0 || strcmp(args[0], "logout") == 0) {
            return 0;
        }

        if (strcmp(args[0], "cd") == 0) {
            const char *target = (n >= 2) ? args[1] : "/";
            if (chdir(target) != 0) {
                printf("cd: %s: %s\n", target, strerror(errno));
            }
            continue;
        }

        int saved_stdout = -1;
        if (apply_stdout_redirection(&n, args, &saved_stdout) == 0) {
            (void)run_external(n, args);
        }
        if (saved_stdout >= 0) {
            (void)dup2(saved_stdout, 1);
            close(saved_stdout);
        }
    }
}
