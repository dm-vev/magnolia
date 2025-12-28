#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define SBRK_ERROR ((char *)-1)
#define SBRK_EAGER 0
#define SBRK_LAZY  1

// system calls (xv6 ABI)
int xv6_fork(void);
void xv6_exit(int) __attribute__((noreturn));
int xv6_wait(int*);
int xv6_pipe(int fds[2]);
ssize_t xv6_write(int, const void*, size_t);
ssize_t xv6_read(int, void*, size_t);
int xv6_close(int);
int xv6_kill(pid_t, int);
int xv6_exec(const char*, char**);
int xv6_open(const char*, int, ...);
int xv6_mknod(const char*, short, short);
int xv6_unlink(const char*);
int xv6_fstat(int fd, struct stat*);
int xv6_link(const char*, const char*);
int xv6_mkdir(const char*, mode_t);
int xv6_chdir(const char*);
int xv6_dup(int);
int xv6_getpid(void);
void* xv6_sys_sbrk(int,int);
int xv6_pause(void);
int xv6_uptime(void);

#define fork() xv6_fork()
#define exit(code) xv6_exit(code)
#define wait(...) xv6_wait(__VA_ARGS__)
#define pipe(...) xv6_pipe(__VA_ARGS__)
#define write(...) xv6_write(__VA_ARGS__)
#define read(...) xv6_read(__VA_ARGS__)
#define close(...) xv6_close(__VA_ARGS__)
#define kill(...) xv6_kill(__VA_ARGS__)
#define exec(...) xv6_exec(__VA_ARGS__)
#define open(...) xv6_open(__VA_ARGS__)
#define mknod(...) xv6_mknod(__VA_ARGS__)
#define unlink(...) xv6_unlink(__VA_ARGS__)
#define fstat(...) xv6_fstat(__VA_ARGS__)
#define link(...) xv6_link(__VA_ARGS__)
#define mkdir(...) xv6_mkdir(__VA_ARGS__)
#define chdir(...) xv6_chdir(__VA_ARGS__)
#define dup(...) xv6_dup(__VA_ARGS__)
#define getpid() xv6_getpid()
#define sys_sbrk(...) xv6_sys_sbrk(__VA_ARGS__)
#define pause() xv6_pause()
#define uptime() xv6_uptime()

// ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, size_t);
char* strchr(const char*, int c);
int strcmp(const char*, const char*);
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, size_t);
int atoi(const char*);
int memcmp(const void *, const void *, size_t);
void *memcpy(void *, const void *, size_t);
void* sbrk(ptrdiff_t);
void* sbrklazy(int);

// printf.c
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"
int fprintf(int, const char*, ...) __attribute__ ((format (printf, 2, 3)));
int printf(const char*, ...) __attribute__ ((format (printf, 1, 2)));
#pragma GCC diagnostic pop

// umalloc.c
void* malloc(uint);
void free(void*);
