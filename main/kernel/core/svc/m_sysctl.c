#include "kernel/core/svc/m_sysctl.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/core/vfs/m_vfs.h"

#ifndef CONFIG_MAGNOLIA_SYSCTL_MAX_FILE_SIZE
#define CONFIG_MAGNOLIA_SYSCTL_MAX_FILE_SIZE 4096
#endif

#ifndef CONFIG_MAGNOLIA_SYSCTL_MAX_LINE
#define CONFIG_MAGNOLIA_SYSCTL_MAX_LINE 256
#endif

#define SYSCTL_PRIMARY_PATH "/config/sysctl.conf"
#define SYSCTL_FALLBACK_PATH "/etc/sysctl.conf"

static int sysctl_errno_from_vfs(m_vfs_error_t err)
{
    switch (err) {
    case M_VFS_ERR_OK:
        return 0;
    case M_VFS_ERR_INVALID_PARAM:
    case M_VFS_ERR_INVALID_PATH:
        return EINVAL;
    case M_VFS_ERR_NOT_FOUND:
        return ENOENT;
    case M_VFS_ERR_NOT_SUPPORTED:
        return ENOTSUP;
    case M_VFS_ERR_NO_MEMORY:
        return ENOMEM;
    case M_VFS_ERR_TOO_MANY_ENTRIES:
        return EMFILE;
    case M_VFS_ERR_BUSY:
        return EBUSY;
    case M_VFS_ERR_INTERRUPTED:
        return EINTR;
    case M_VFS_ERR_WOULD_BLOCK:
        return EAGAIN;
    case M_VFS_ERR_TIMEOUT:
        return ETIMEDOUT;
    case M_VFS_ERR_IO:
        return EIO;
    case M_VFS_ERR_DESTROYED:
        return EBADF;
    default:
        return EIO;
    }
}

static bool sysctl_key_valid(const char *key)
{
    if (key == NULL || key[0] == '\0') {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)key; *p != '\0'; ++p) {
        if (isalnum(*p) || *p == '_' || *p == '.' || *p == '-') {
            continue;
        }
        return false;
    }
    return true;
}

static void sysctl_trim(char *value)
{
    if (value == NULL) {
        return;
    }

    char *start = value;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }

    char *end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1])) {
        end--;
    }

    size_t len = (size_t)(end - start);
    if (start != value && len > 0) {
        memmove(value, start, len);
    }
    value[len] = '\0';
}

static bool sysctl_parse_line(char *line, char **out_key, char **out_value)
{
    if (line == NULL || out_key == NULL || out_value == NULL) {
        return false;
    }

    sysctl_trim(line);
    if (line[0] == '\0' || line[0] == '#' || line[0] == ';') {
        return false;
    }

    char *eq = strchr(line, '=');
    if (eq == NULL) {
        return false;
    }
    *eq = '\0';

    char *key = line;
    char *value = eq + 1;
    sysctl_trim(key);
    sysctl_trim(value);

    if (key[0] == '\0') {
        return false;
    }

    *out_key = key;
    *out_value = value;
    return true;
}

static int sysctl_open_file(const char *path, int flags, int *out_fd)
{
    if (path == NULL || out_fd == NULL) {
        return -EINVAL;
    }
    m_vfs_error_t err = m_vfs_open(NULL, path, flags, out_fd);
    if (err != M_VFS_ERR_OK) {
        return -sysctl_errno_from_vfs(err);
    }
    return 0;
}

static int sysctl_read_file(const char *path, char **out_buf, size_t *out_len)
{
    if (path == NULL || out_buf == NULL || out_len == NULL) {
        return -EINVAL;
    }

    int fd = -1;
    int rc = sysctl_open_file(path, O_RDONLY, &fd);
    if (rc != 0) {
        return rc;
    }

    size_t cap = CONFIG_MAGNOLIA_SYSCTL_MAX_FILE_SIZE;
    char *buffer = malloc(cap + 1);
    if (buffer == NULL) {
        m_vfs_close(NULL, fd);
        return -ENOMEM;
    }

    size_t used = 0;
    while (used < cap) {
        size_t read = 0;
        m_vfs_error_t err = m_vfs_read(NULL,
                                       fd,
                                       buffer + used,
                                       cap - used,
                                       &read);
        if (err != M_VFS_ERR_OK) {
            free(buffer);
            m_vfs_close(NULL, fd);
            return -sysctl_errno_from_vfs(err);
        }
        if (read == 0) {
            break;
        }
        used += read;
    }

    m_vfs_close(NULL, fd);

    if (used >= cap) {
        free(buffer);
        return -EFBIG;
    }

    buffer[used] = '\0';
    *out_buf = buffer;
    *out_len = used;
    return 0;
}

static int sysctl_write_file(const char *path, const char *data, size_t len)
{
    if (path == NULL || data == NULL) {
        return -EINVAL;
    }

    int fd = -1;
    int rc = sysctl_open_file(path, O_WRONLY | O_CREAT | O_TRUNC, &fd);
    if (rc != 0) {
        return rc;
    }

    size_t written_total = 0;
    while (written_total < len) {
        size_t written = 0;
        m_vfs_error_t err = m_vfs_write(NULL,
                                        fd,
                                        data + written_total,
                                        len - written_total,
                                        &written);
        if (err != M_VFS_ERR_OK) {
            m_vfs_close(NULL, fd);
            return -sysctl_errno_from_vfs(err);
        }
        if (written == 0) {
            m_vfs_close(NULL, fd);
            return -EIO;
        }
        written_total += written;
    }

    m_vfs_close(NULL, fd);
    return 0;
}

static int sysctl_load(char **out_buf,
                       size_t *out_len,
                       const char **out_path,
                       bool allow_missing)
{
    const char *paths[] = { SYSCTL_PRIMARY_PATH, SYSCTL_FALLBACK_PATH };
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        char *buf = NULL;
        size_t len = 0;
        int rc = sysctl_read_file(paths[i], &buf, &len);
        if (rc == 0) {
            *out_buf = buf;
            *out_len = len;
            if (out_path != NULL) {
                *out_path = paths[i];
            }
            return 0;
        }
        if (rc != -ENOENT) {
            return rc;
        }
    }

    if (allow_missing) {
        *out_buf = NULL;
        *out_len = 0;
        if (out_path != NULL) {
            *out_path = NULL;
        }
        return 0;
    }

    return -ENOENT;
}

static int sysctl_append(char *out, size_t *used, size_t cap, const char *line, size_t len)
{
    if (*used + len > cap) {
        return -ENOSPC;
    }
    memcpy(out + *used, line, len);
    *used += len;
    return 0;
}

static int sysctl_build_update(const char *input,
                               size_t input_len,
                               const char *key,
                               const char *value,
                               char **out_buf,
                               size_t *out_len)
{
    size_t cap = CONFIG_MAGNOLIA_SYSCTL_MAX_FILE_SIZE;
    char *out = malloc(cap + 1);
    if (out == NULL) {
        return -ENOMEM;
    }

    size_t used = 0;
    bool replaced = false;

    const char *cursor = input;
    const char *end = input + input_len;
    while (cursor < end) {
        const char *line_start = cursor;
        const char *newline = memchr(cursor, '\n', (size_t)(end - cursor));
        size_t line_len = newline ? (size_t)(newline - cursor) : (size_t)(end - cursor);
        cursor = newline ? newline + 1 : end;

        if (line_len >= CONFIG_MAGNOLIA_SYSCTL_MAX_LINE) {
            free(out);
            return -E2BIG;
        }

        char line[CONFIG_MAGNOLIA_SYSCTL_MAX_LINE];
        memcpy(line, line_start, line_len);
        line[line_len] = '\0';

        char *parsed_key = NULL;
        char *parsed_value = NULL;
        if (sysctl_parse_line(line, &parsed_key, &parsed_value) &&
                strcmp(parsed_key, key) == 0) {
            if (!replaced) {
                char replacement[CONFIG_MAGNOLIA_SYSCTL_MAX_LINE];
                int written = snprintf(replacement,
                                       sizeof(replacement),
                                       "%s=%s",
                                       key,
                                       value);
                if (written < 0 || (size_t)written >= sizeof(replacement)) {
                    free(out);
                    return -E2BIG;
                }
                int rc = sysctl_append(out, &used, cap, replacement, (size_t)written);
                if (rc != 0) {
                    free(out);
                    return rc;
                }
                replaced = true;
            }
        } else {
            int rc = sysctl_append(out, &used, cap, line_start, line_len);
            if (rc != 0) {
                free(out);
                return rc;
            }
        }

        if (newline != NULL || cursor < end) {
            int rc = sysctl_append(out, &used, cap, "\n", 1);
            if (rc != 0) {
                free(out);
                return rc;
            }
        }
    }

    if (!replaced) {
        if (used > 0 && out[used - 1] != '\n') {
            int rc = sysctl_append(out, &used, cap, "\n", 1);
            if (rc != 0) {
                free(out);
                return rc;
            }
        }

        char replacement[CONFIG_MAGNOLIA_SYSCTL_MAX_LINE];
        int written = snprintf(replacement,
                               sizeof(replacement),
                               "%s=%s\n",
                               key,
                               value);
        if (written < 0 || (size_t)written >= sizeof(replacement)) {
            free(out);
            return -E2BIG;
        }
        int rc = sysctl_append(out, &used, cap, replacement, (size_t)written);
        if (rc != 0) {
            free(out);
            return rc;
        }
    }

    out[used] = '\0';
    *out_buf = out;
    *out_len = used;
    return 0;
}

static void sysctl_free(char *buf)
{
    if (buf != NULL) {
        free(buf);
    }
}

static void sysctl_ensure_dir(const char *path)
{
    if (path == NULL) {
        return;
    }
    (void)m_vfs_mkdir(NULL, path, M_VFS_DIRECTORY_MODE_DEFAULT);
}

int m_sysctl_get(const char *key, char *out_value, size_t value_size)
{
    if (key == NULL || out_value == NULL || value_size == 0) {
        return -EINVAL;
    }
    if (!sysctl_key_valid(key)) {
        return -EINVAL;
    }

    char *buf = NULL;
    size_t len = 0;
    int rc = sysctl_load(&buf, &len, NULL, false);
    if (rc != 0) {
        return rc;
    }

    const char *cursor = buf;
    const char *end = buf + len;
    while (cursor < end) {
        const char *line_start = cursor;
        const char *newline = memchr(cursor, '\n', (size_t)(end - cursor));
        size_t line_len = newline ? (size_t)(newline - cursor) : (size_t)(end - cursor);
        cursor = newline ? newline + 1 : end;

        if (line_len >= CONFIG_MAGNOLIA_SYSCTL_MAX_LINE) {
            sysctl_free(buf);
            return -E2BIG;
        }

        char line[CONFIG_MAGNOLIA_SYSCTL_MAX_LINE];
        memcpy(line, line_start, line_len);
        line[line_len] = '\0';

        char *parsed_key = NULL;
        char *parsed_value = NULL;
        if (sysctl_parse_line(line, &parsed_key, &parsed_value) &&
                strcmp(parsed_key, key) == 0) {
            size_t needed = strlen(parsed_value) + 1;
            if (needed > value_size) {
                sysctl_free(buf);
                return -ENOSPC;
            }
            memcpy(out_value, parsed_value, needed);
            sysctl_free(buf);
            return 0;
        }
    }

    sysctl_free(buf);
    return -ENOENT;
}

int m_sysctl_set(const char *key, const char *value)
{
    if (key == NULL || value == NULL) {
        return -EINVAL;
    }
    if (!sysctl_key_valid(key)) {
        return -EINVAL;
    }

    char *buf = NULL;
    size_t len = 0;
    const char *src_path = NULL;
    int rc = sysctl_load(&buf, &len, &src_path, true);
    if (rc != 0) {
        return rc;
    }

    const char *target_path = src_path ? src_path : SYSCTL_PRIMARY_PATH;
    if (strncmp(target_path, "/config/", 8) == 0) {
        sysctl_ensure_dir("/config");
    } else if (strncmp(target_path, "/etc/", 5) == 0) {
        sysctl_ensure_dir("/etc");
    }

    char *out = NULL;
    size_t out_len = 0;
    rc = sysctl_build_update(buf ? buf : "", buf ? len : 0, key, value, &out, &out_len);
    sysctl_free(buf);
    if (rc != 0) {
        sysctl_free(out);
        return rc;
    }

    char tmp_path[M_VFS_PATH_MAX_LEN];
    int written = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", target_path);
    if (written < 0 || (size_t)written >= sizeof(tmp_path)) {
        sysctl_free(out);
        return -ENAMETOOLONG;
    }

    rc = sysctl_write_file(tmp_path, out, out_len);
    if (rc != 0) {
        sysctl_free(out);
        return rc;
    }

    m_vfs_error_t err = m_vfs_rename(NULL, tmp_path, target_path);
    if (err != M_VFS_ERR_OK) {
        (void)m_vfs_unlink(NULL, tmp_path);
        sysctl_free(out);
        return -sysctl_errno_from_vfs(err);
    }

    sysctl_free(out);
    return 0;
}

int m_sysctl_list(const char *prefix, m_sysctl_list_cb cb, void *ctx)
{
    if (cb == NULL) {
        return -EINVAL;
    }

    char *buf = NULL;
    size_t len = 0;
    int rc = sysctl_load(&buf, &len, NULL, true);
    if (rc != 0) {
        return rc;
    }

    if (buf == NULL || len == 0) {
        sysctl_free(buf);
        return 0;
    }

    size_t prefix_len = 0;
    if (prefix != NULL) {
        prefix_len = strlen(prefix);
    }

    const char *cursor = buf;
    const char *end = buf + len;
    while (cursor < end) {
        const char *line_start = cursor;
        const char *newline = memchr(cursor, '\n', (size_t)(end - cursor));
        size_t line_len = newline ? (size_t)(newline - cursor) : (size_t)(end - cursor);
        cursor = newline ? newline + 1 : end;

        if (line_len >= CONFIG_MAGNOLIA_SYSCTL_MAX_LINE) {
            sysctl_free(buf);
            return -E2BIG;
        }

        char line[CONFIG_MAGNOLIA_SYSCTL_MAX_LINE];
        memcpy(line, line_start, line_len);
        line[line_len] = '\0';

        char *parsed_key = NULL;
        char *parsed_value = NULL;
        if (!sysctl_parse_line(line, &parsed_key, &parsed_value)) {
            continue;
        }

        if (prefix_len > 0 && strncmp(parsed_key, prefix, prefix_len) != 0) {
            continue;
        }

        int stop = cb(parsed_key, parsed_value, ctx);
        if (stop != 0) {
            sysctl_free(buf);
            return 0;
        }
    }

    sysctl_free(buf);
    return 0;
}
