/*
 * Magnolia ELF applet: elftest.
 * Runs minimal runtime/VFS/libc/allocator checks and returns 0 on success.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static int test_entry_exit(void)
{
    printf("entry/exit test start\n");
    printf("entry/exit test ok\n");
    return 0;
}

static int test_allocator(void)
{
    printf("allocator test start\n");

    char *p = malloc(32);
    if (!p) return 1;
    memset(p, 0xAA, 32);

    char *q = calloc(4, 8);
    if (!q) { free(p); return 1; }
    for (int i = 0; i < 32; ++i) {
        if (q[i] != 0) { free(p); free(q); return 1; }
    }

    char *r = realloc(p, 64);
    if (!r) { free(p); free(q); return 1; }
    for (int i = 0; i < 32; ++i) {
        if ((unsigned char)r[i] != 0xAA) { free(r); free(q); return 1; }
    }

    free(r);
    free(q);
    printf("allocator test ok\n");
    return 0;
}

static int test_libc_basic(void)
{
    printf("libc test start\n");
    char buf[16];
    memset(buf, 0, sizeof(buf));
    const char *msg = "hello";
    memcpy(buf, msg, strlen(msg));
    if (strlen(buf) != 5) return 1;
    if (strcmp(buf, "hello") != 0) return 1;
    char out[32];
    snprintf(out, sizeof(out), "x=%d", 7);
    if (strcmp(out, "x=7") != 0) return 1;
    printf("libc test ok\n");
    return 0;
}

static int test_vfs_rw(void)
{
    printf("vfs test start\n");
    const char *path = "/flash/elftest_tmp";
    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0666);
    if (fd < 0) return 1;

    const char *payload = "magnolia";
    ssize_t w = write(fd, payload, strlen(payload));
    if (w != (ssize_t)strlen(payload)) { close(fd); return 1; }

    lseek(fd, 0, SEEK_SET);
    char buf[16] = {0};
    ssize_t r = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (r != (ssize_t)strlen(payload)) return 1;
    if (strcmp(buf, payload) != 0) return 1;

    unlink(path);
    printf("vfs test ok\n");
    return 0;
}

static int test_error_path(void)
{
    printf("error path test start\n");
    errno = 0;
    int fd = open("/flash/no_such_file", O_RDONLY);
    if (fd >= 0) { close(fd); return 1; }
    if (errno == 0) return 1;
    printf("error path test ok errno=%d\n", errno);
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    int fails = 0;
    fails += test_entry_exit();
    fails += test_allocator();
    fails += test_libc_basic();
    fails += test_vfs_rw();
    fails += test_error_path();

    printf("elftest finished fails=%d\n", fails);
    return fails ? 1 : 0;
}

