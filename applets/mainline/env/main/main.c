#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

/*
 * BSD reference: FreeBSD env(1)
 *
 * Behavior notes:
 * - Empty PATH elements are treated as the current directory.
 * - -S word splitting follows FreeBSD quoting/escape rules.
 */

static const char *k_default_path = "/usr/bin:/bin";

#ifdef ESP_PLATFORM
static int execv_stub(const char *path, char *const argv[])
{
    (void)path;
    (void)argv;
    errno = ENOSYS;
    return -1;
}
#define execv execv_stub
#endif

typedef struct {
    char **argv;
    int argc;
    char **owned;
    size_t owned_count;
    size_t owned_cap;
    bool owns_argv;
} arglist_t;

typedef struct {
    const char **items;
    size_t count;
    size_t cap;
} ptrlist_t;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} strbuf_t;

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
    (void)write_all(STDERR_FILENO, buf, len);
}

static void usage(void)
{
    eprintf("usage: env [-i] [-u name] [-P utilpath] [-S string] [-v] [name=value ...] [utility [argument ...]]\n");
}

static bool is_assignment(const char *s)
{
    if (!s) {
        return false;
    }
    const char *eq = strchr(s, '=');
    return eq && eq != s;
}

static int ptrlist_add(ptrlist_t *list, const char *item)
{
    if (!list || !item) {
        errno = EINVAL;
        return -1;
    }
    if (list->count == list->cap) {
        size_t next = list->cap ? (list->cap * 2u) : 8u;
        if (next < list->cap || next > SIZE_MAX / sizeof(*list->items)) {
            errno = EOVERFLOW;
            return -1;
        }
        const char **tmp = (const char **)realloc(list->items, next * sizeof(*list->items));
        if (!tmp) {
            return -1;
        }
        list->items = tmp;
        list->cap = next;
    }
    list->items[list->count++] = item;
    return 0;
}

static int arglist_add_owned(arglist_t *args, char *item)
{
    if (!args || !item) {
        errno = EINVAL;
        return -1;
    }
    if (args->owned_count == args->owned_cap) {
        size_t next = args->owned_cap ? (args->owned_cap * 2u) : 8u;
        if (next < args->owned_cap || next > SIZE_MAX / sizeof(*args->owned)) {
            errno = EOVERFLOW;
            return -1;
        }
        char **tmp = (char **)realloc(args->owned, next * sizeof(*args->owned));
        if (!tmp) {
            return -1;
        }
        args->owned = tmp;
        args->owned_cap = next;
    }
    args->owned[args->owned_count++] = item;
    return 0;
}

static void arglist_init(arglist_t *args, int argc, char **argv)
{
    args->argv = argv;
    args->argc = argc;
    args->owned = NULL;
    args->owned_count = 0;
    args->owned_cap = 0;
    args->owns_argv = false;
}

static void arglist_free(arglist_t *args)
{
    if (!args) {
        return;
    }
    for (size_t i = 0; i < args->owned_count; ++i) {
        free(args->owned[i]);
    }
    free(args->owned);
    if (args->owns_argv) {
        free(args->argv);
    }
}

static void strbuf_init(strbuf_t *sb)
{
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static void strbuf_free(strbuf_t *sb)
{
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static int strbuf_reserve(strbuf_t *sb, size_t add)
{
    if (sb->len + add + 1 < sb->len) {
        errno = EOVERFLOW;
        return -1;
    }
    size_t need = sb->len + add + 1;
    if (need <= sb->cap) {
        return 0;
    }
    size_t next = sb->cap ? (sb->cap * 2u) : 32u;
    if (next < need) {
        next = need;
    }
    if (next < sb->cap || next > SIZE_MAX / sizeof(char)) {
        errno = EOVERFLOW;
        return -1;
    }
    char *tmp = (char *)realloc(sb->data, next);
    if (!tmp) {
        return -1;
    }
    sb->data = tmp;
    sb->cap = next;
    return 0;
}

static int strbuf_push(strbuf_t *sb, char c)
{
    if (strbuf_reserve(sb, 1) != 0) {
        return -1;
    }
    sb->data[sb->len++] = c;
    return 0;
}

static char *strbuf_finish(strbuf_t *sb)
{
    if (strbuf_reserve(sb, 0) != 0) {
        return NULL;
    }
    sb->data[sb->len] = '\0';
    char *out = sb->data;
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
    return out;
}

static int split_words(const char *s, char ***out_tokens, size_t *out_count, const char **out_err)
{
    if (out_tokens) {
        *out_tokens = NULL;
    }
    if (out_count) {
        *out_count = 0;
    }
    if (out_err) {
        *out_err = NULL;
    }
    if (!s || !out_tokens || !out_count) {
        errno = EINVAL;
        return -1;
    }

    enum { QUOTE_NONE, QUOTE_SINGLE, QUOTE_DOUBLE } quote = QUOTE_NONE;
    char **tokens = NULL;
    size_t count = 0;
    size_t cap = 0;
    const char *p = s;

    while (*p) {
        while (*p && isspace((unsigned char)*p)) {
            ++p;
        }
        if (!*p) {
            break;
        }
        strbuf_t sb;
        strbuf_init(&sb);
        quote = QUOTE_NONE;
        while (*p) {
            char c = *p;
            if (quote == QUOTE_NONE) {
                if (isspace((unsigned char)c)) {
                    ++p;
                    break;
                }
                if (c == '\'') {
                    quote = QUOTE_SINGLE;
                    ++p;
                    continue;
                }
                if (c == '"') {
                    quote = QUOTE_DOUBLE;
                    ++p;
                    continue;
                }
                if (c == '\\') {
                    ++p;
                    if (*p) {
                        if (strbuf_push(&sb, *p++) != 0) {
                            strbuf_free(&sb);
                            goto fail;
                        }
                    } else {
                        if (strbuf_push(&sb, '\\') != 0) {
                            strbuf_free(&sb);
                            goto fail;
                        }
                    }
                    continue;
                }
                if (strbuf_push(&sb, c) != 0) {
                    strbuf_free(&sb);
                    goto fail;
                }
                ++p;
                continue;
            }
            if (quote == QUOTE_SINGLE) {
                if (c == '\'') {
                    quote = QUOTE_NONE;
                    ++p;
                    continue;
                }
                if (strbuf_push(&sb, c) != 0) {
                    strbuf_free(&sb);
                    goto fail;
                }
                ++p;
                continue;
            }
            if (c == '"') {
                quote = QUOTE_NONE;
                ++p;
                continue;
            }
            if (c == '\\') {
                ++p;
                if (*p) {
                    if (strbuf_push(&sb, *p++) != 0) {
                        strbuf_free(&sb);
                        goto fail;
                    }
                } else {
                    if (strbuf_push(&sb, '\\') != 0) {
                        strbuf_free(&sb);
                        goto fail;
                    }
                }
                continue;
            }
            if (strbuf_push(&sb, c) != 0) {
                strbuf_free(&sb);
                goto fail;
            }
            ++p;
        }

        if (quote != QUOTE_NONE) {
            strbuf_free(&sb);
            if (out_err) {
                *out_err = "unterminated quote";
            }
            errno = EINVAL;
            goto fail;
        }

        char *token = strbuf_finish(&sb);
        if (!token) {
            strbuf_free(&sb);
            goto fail;
        }

        if (count == cap) {
            size_t next = cap ? (cap * 2u) : 8u;
            if (next < cap || next > SIZE_MAX / sizeof(*tokens)) {
                free(token);
                errno = EOVERFLOW;
                goto fail;
            }
            char **tmp = (char **)realloc(tokens, next * sizeof(*tokens));
            if (!tmp) {
                free(token);
                goto fail;
            }
            tokens = tmp;
            cap = next;
        }
        tokens[count++] = token;
    }

    *out_tokens = tokens;
    *out_count = count;
    return 0;

fail:
    for (size_t i = 0; i < count; ++i) {
        free(tokens[i]);
    }
    free(tokens);
    return -1;
}

static int expand_S(arglist_t *args, int idx, const char *spec, int remove_count)
{
    const char *err = NULL;
    char **tokens = NULL;
    size_t count = 0;
    if (split_words(spec, &tokens, &count, &err) != 0) {
        if (err) {
            eprintf("env: -S: %s\n", err);
        } else {
            eprintf("env: -S: %s\n", strerror(errno));
        }
        return -1;
    }
    if (count == 0) {
        free(tokens);
        eprintf("env: -S: empty argument\n");
        return -1;
    }

    for (size_t i = 0; i < count; ++i) {
        if (arglist_add_owned(args, tokens[i]) != 0) {
            for (size_t j = i; j < count; ++j) {
                free(tokens[j]);
            }
            free(tokens);
            eprintf("env: -S: %s\n", strerror(errno));
            return -1;
        }
    }

    if (remove_count <= 0 || idx < 0 || idx + remove_count > args->argc) {
        free(tokens);
        errno = EINVAL;
        return -1;
    }

    size_t new_argc = (size_t)args->argc - (size_t)remove_count + count;
    if (new_argc > (size_t)INT_MAX || new_argc + 1u < new_argc) {
        free(tokens);
        errno = EOVERFLOW;
        return -1;
    }
    char **new_argv = (char **)calloc(new_argc + 1u, sizeof(char *));
    if (!new_argv) {
        free(tokens);
        return -1;
    }

    size_t out = 0;
    for (int i = 0; i < idx; ++i) {
        new_argv[out++] = args->argv[i];
    }
    for (size_t i = 0; i < count; ++i) {
        new_argv[out++] = tokens[i];
    }
    for (int i = idx + remove_count; i < args->argc; ++i) {
        new_argv[out++] = args->argv[i];
    }
    new_argv[out] = NULL;

    if (args->owns_argv) {
        free(args->argv);
    }
    args->argv = new_argv;
    args->argc = (int)new_argc;
    args->owns_argv = true;

    free(tokens);
    return 0;
}

static int clear_environment(void)
{
    if (!environ) {
        return 0;
    }
    size_t count = 0;
    for (char **env = environ; env && *env; ++env) {
        ++count;
    }
    char **names = NULL;
    if (count > 0) {
        if (count > SIZE_MAX / sizeof(*names)) {
            errno = EOVERFLOW;
            return -1;
        }
        names = (char **)calloc(count, sizeof(*names));
        if (!names) {
            return -1;
        }
    }

    size_t used = 0;
    for (char **env = environ; env && *env; ++env) {
        const char *eq = strchr(*env, '=');
        if (!eq) {
            continue;
        }
        size_t len = (size_t)(eq - *env);
        if (len == 0) {
            continue;
        }
        char *name = (char *)malloc(len + 1u);
        if (!name) {
            for (size_t i = 0; i < used; ++i) {
                free(names[i]);
            }
            free(names);
            return -1;
        }
        memcpy(name, *env, len);
        name[len] = '\0';
        names[used++] = name;
    }

    int saved_errno = 0;
    for (size_t i = 0; i < used; ++i) {
        if (unsetenv(names[i]) != 0 && saved_errno == 0) {
            saved_errno = errno;
        }
        free(names[i]);
    }
    free(names);
    if (saved_errno != 0) {
        errno = saved_errno;
        return -1;
    }
    return 0;
}

static int print_env_stream(int fd)
{
    if (!environ) {
        return 0;
    }
    for (size_t i = 0; environ[i]; ++i) {
        const char *line = environ[i];
        size_t len = strlen(line);
        if ((len && write_all(fd, line, len) != 0) || write_all(fd, "\n", 1) != 0) {
            return -1;
        }
    }
    return 0;
}

static int print_command(int fd, char *const *argv)
{
    if (!argv || !argv[0]) {
        return 0;
    }
    if (write_all(fd, "env: ", 5) != 0) {
        return -1;
    }
    for (int i = 0; argv[i]; ++i) {
        if (i > 0 && write_all(fd, " ", 1) != 0) {
            return -1;
        }
        size_t len = strlen(argv[i]);
        if (len && write_all(fd, argv[i], len) != 0) {
            return -1;
        }
    }
    return write_all(fd, "\n", 1);
}

static int exec_with_path(const char *file, char *const argv[], const char *path)
{
    if (!file || !*file) {
        errno = ENOENT;
        return -1;
    }
    if (strchr(file, '/')) {
        execv(file, argv);
        return -1;
    }

    const char *path_list = path;
    if (!path_list) {
        path_list = getenv("PATH");
        if (!path_list) {
            path_list = k_default_path;
        }
    }

    bool saw_eacces = false;
    int saved_errno = ENOENT;
    size_t file_len = strlen(file);
    const char *p = path_list;

    while (1) {
        const char *sep = strchr(p, ':');
        size_t dir_len = sep ? (size_t)(sep - p) : strlen(p);
        const char *dir = p;
        size_t prefix_len = dir_len;
        if (prefix_len == 0) {
            execv(file, argv);
        } else {
            size_t extra = (dir[prefix_len - 1] == '/') ? 0u : 1u;
            if (prefix_len > SIZE_MAX - file_len - extra - 1u) {
                errno = ENAMETOOLONG;
                return -1;
            }
            size_t total = prefix_len + extra + file_len + 1u;
            char *candidate = (char *)malloc(total);
            if (!candidate) {
                return -1;
            }
            memcpy(candidate, dir, prefix_len);
            size_t off = prefix_len;
            if (extra) {
                candidate[off++] = '/';
            }
            memcpy(candidate + off, file, file_len);
            candidate[off + file_len] = '\0';
            execv(candidate, argv);
            free(candidate);
        }

        if (errno == EACCES) {
            saw_eacces = true;
        } else if (errno != ENOENT && errno != ENOTDIR && errno != EACCES) {
            saved_errno = errno;
        }

        if (!sep) {
            break;
        }
        p = sep + 1;
    }

    if (saw_eacces) {
        errno = EACCES;
    } else {
        errno = saved_errno;
    }
    return -1;
}

static int dup_range(const char *s, size_t len, char **out)
{
    if (!out) {
        errno = EINVAL;
        return -1;
    }
    char *buf = (char *)malloc(len + 1u);
    if (!buf) {
        return -1;
    }
    if (len) {
        memcpy(buf, s, len);
    }
    buf[len] = '\0';
    *out = buf;
    return 0;
}

int main(int argc, char **argv)
{
    arglist_t args;
    arglist_init(&args, argc, argv);

    ptrlist_t unset_names = { NULL, 0, 0 };
    bool clear_env = false;
    bool verbose = false;
    const char *utilpath = NULL;
    int exit_status = 1;

    int i = 1;
    while (i < args.argc) {
        const char *arg = args.argv[i];
        if (!arg) {
            ++i;
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            ++i;
            break;
        }
        if (strcmp(arg, "-") == 0) {
            clear_env = true;
            ++i;
            continue;
        }
        if (arg[0] != '-' || arg[1] == '\0') {
            break;
        }

        bool consumed_next = false;
        bool restart = false;
        const char *opt = arg + 1;
        while (*opt) {
            char c = *opt++;
            if (c == 'i') {
                clear_env = true;
                continue;
            }
            if (c == 'v') {
                verbose = true;
                continue;
            }
            if (c == 'u' || c == 'P' || c == 'S') {
                const char *optarg = NULL;
                bool inline_arg = false;
                if (*opt != '\0') {
                    optarg = opt;
                    inline_arg = true;
                } else {
                    if (i + 1 >= args.argc) {
                        eprintf("env: option requires an argument -- %c\n", c);
                        usage();
                        goto cleanup;
                    }
                    optarg = args.argv[i + 1];
                }

                if (c == 'u') {
                    if (ptrlist_add(&unset_names, optarg) != 0) {
                        eprintf("env: %s\n", strerror(errno));
                        goto cleanup;
                    }
                } else if (c == 'P') {
                    utilpath = optarg;
                } else {
                    int remove_count = inline_arg ? 1 : 2;
                    if (expand_S(&args, i, optarg, remove_count) != 0) {
                        goto cleanup;
                    }
                    restart = true;
                }

                if (!inline_arg) {
                    consumed_next = true;
                }
                break;
            }

            eprintf("env: illegal option -- %c\n", c);
            usage();
            goto cleanup;
        }

        if (restart) {
            continue;
        }
        if (consumed_next) {
            i += 2;
        } else {
            ++i;
        }
    }

    if (clear_env) {
        if (clear_environment() != 0) {
            eprintf("env: clearenv: %s\n", strerror(errno));
            goto cleanup;
        }
    }

    for (size_t u = 0; u < unset_names.count; ++u) {
        const char *name = unset_names.items[u];
        if (unsetenv(name) != 0) {
            eprintf("env: unsetenv %s: %s\n", name, strerror(errno));
            goto cleanup;
        }
    }

    while (i < args.argc && is_assignment(args.argv[i])) {
        const char *eq = strchr(args.argv[i], '=');
        size_t name_len = (size_t)(eq - args.argv[i]);
        char *name = NULL;
        if (dup_range(args.argv[i], name_len, &name) != 0) {
            eprintf("env: %s\n", strerror(errno));
            goto cleanup;
        }
        const char *val = eq + 1;
        if (setenv(name, val, 1) != 0) {
            eprintf("env: setenv %s: %s\n", name, strerror(errno));
            free(name);
            goto cleanup;
        }
        free(name);
        ++i;
    }

    if (i >= args.argc) {
        if (print_env_stream(STDOUT_FILENO) != 0) {
            eprintf("env: stdout: %s\n", strerror(errno));
            goto cleanup;
        }
        exit_status = 0;
        goto cleanup;
    }

    char **cmd_argv = &args.argv[i];
    if (verbose && print_command(STDERR_FILENO, cmd_argv) != 0) {
        eprintf("env: stderr: %s\n", strerror(errno));
        goto cleanup;
    }

    if (exec_with_path(cmd_argv[0], cmd_argv, utilpath) != 0) {
        int err = errno;
        eprintf("env: %s: %s\n", cmd_argv[0], strerror(err));
        exit_status = (err == ENOENT) ? 127 : 126;
        goto cleanup;
    }

cleanup:
    arglist_free(&args);
    free(unset_names.items);
    return exit_status;
}
