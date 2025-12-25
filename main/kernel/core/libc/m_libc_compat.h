/*
 * Magnolia libc compatibility layer for ELF applets.
 *
 * These functions are intended to be exported as ABI symbols to dynamically
 * linked ELF applets. They provide thin wrappers over Magnolia kernel APIs.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <setjmp.h>
#include <sys/reent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <time.h>
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct m_libc_exit_frame {
    jmp_buf env;
    int code;
    void *prev;
} m_libc_exit_frame_t;

void m_libc_exit_frame_push(m_libc_exit_frame_t *frame);
void m_libc_exit_frame_pop(m_libc_exit_frame_t *frame);

void m_libc_exit(int status);
void m_libc__exit(int status);
void m_libc_abort(void);
int m_libc_atexit(void (*fn)(void));
int m_libc___cxa_atexit(void (*fn)(void *), void *arg, void *dso);
void m_libc___cxa_finalize(void *dso);

int *m_libc___errno(void);

int m_libc_open(const char *path, int flags, ...);
int m_libc_close(int fd);
ssize_t m_libc_read(int fd, void *buffer, size_t size);
ssize_t m_libc_write(int fd, const void *buffer, size_t size);
off_t m_libc_lseek(int fd, off_t offset, int whence);
int m_libc_ioctl(int fd, unsigned long request, ...);
int m_libc_dup(int oldfd);
int m_libc_dup2(int oldfd, int newfd);
int m_libc_poll(void *fds, unsigned long nfds, int timeout_ms);
int m_libc_isatty(int fd);
int m_libc_access(const char *path, int mode);
int m_libc_remove(const char *path);

void *m_libc_malloc(size_t size);
void *m_libc_calloc(size_t nmemb, size_t size);
void *m_libc_realloc(void *ptr, size_t size);
void m_libc_free(void *ptr);

int m_libc_unlink(const char *path);
int m_libc_mkdir(const char *path, mode_t mode);
int m_libc_chdir(const char *path);
char *m_libc_getcwd(char *buffer, size_t size);
int m_libc_stat(const char *path, void *out_stat);
int m_libc_fstat(int fd, void *out_stat);

void *m_libc_opendir(const char *path);
void *m_libc_readdir(void *dirp);
int m_libc_closedir(void *dirp);
void m_libc_rewinddir(void *dirp);

int m_libc_getpid(void);
int m_libc_getppid(void);
unsigned int m_libc_getuid(void);
unsigned int m_libc_getgid(void);
unsigned int m_libc_geteuid(void);
unsigned int m_libc_getegid(void);

int m_libc_clock_gettime(int clock_id, void *tp);
int m_libc_gettimeofday(void *tv, void *tz);
time_t m_libc_time(time_t *tloc);
unsigned int m_libc_sleep(unsigned int seconds);
int m_libc_usleep(unsigned int usec);
int m_libc_nanosleep(const void *req, void *rem);

#if defined(CONFIG_MAGNOLIA_ELF_EXPORT_NEWLIB) && CONFIG_MAGNOLIA_ELF_EXPORT_NEWLIB
void *m_libc_malloc_r(struct _reent *r, size_t size);
void *m_libc_calloc_r(struct _reent *r, size_t nmemb, size_t size);
void *m_libc_realloc_r(struct _reent *r, void *ptr, size_t size);
void m_libc_free_r(struct _reent *r, void *ptr);

int m_libc_open_r(struct _reent *r, const char *file, int flags, int mode);
int m_libc_close_r(struct _reent *r, int fd);
_ssize_t m_libc_read_r(struct _reent *r, int fd, void *buf, size_t cnt);
_ssize_t m_libc_write_r(struct _reent *r, int fd, const void *buf, size_t cnt);
_off_t m_libc_lseek_r(struct _reent *r, int fd, _off_t pos, int whence);
int m_libc_fstat_r(struct _reent *r, int fd, struct stat *st);
int m_libc_stat_r(struct _reent *r, const char *file, struct stat *st);
int m_libc_isatty_r(struct _reent *r, int fd);
int m_libc_unlink_r(struct _reent *r, const char *file);
int m_libc_mkdir_r(struct _reent *r, const char *path, int mode);
int m_libc_chdir_r(struct _reent *r, const char *path);
char *m_libc_getcwd_r(struct _reent *r, char *buf, size_t size);
int m_libc_rename_r(struct _reent *r, const char *old, const char *newpath);
int m_libc_link_r(struct _reent *r, const char *old, const char *newpath);
int m_libc_rmdir_r(struct _reent *r, const char *path);

/* Avoid colliding with ESP-IDF's strong syscall aliases (_sbrk_r/_kill_r/_getpid_r/_exit). */
int m_libc_gettimeofday_r(struct _reent *r, struct timeval *tv, void *tzp);
clock_t m_libc_times_r(struct _reent *r, struct tms *buf);
void *m_libc_sbrk_r(struct _reent *r, ptrdiff_t incr);
int m_libc_kill_r(struct _reent *r, int pid, int sig);
int m_libc_getpid_r(struct _reent *r);
#endif

#ifdef __cplusplus
}
#endif
