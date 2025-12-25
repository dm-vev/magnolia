#include "kernel/core/libc/m_libc_compat.h"

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/reent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <time.h>
#include <unistd.h>

#include "esp_rom_sys.h"
#include "esp_rom_serial_output.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "kernel/arch/m_arch.h"
#include "kernel/core/job/jctx.h"
#include "kernel/core/memory/m_alloc.h"
#include "kernel/core/timer/m_timer_deadline.h"
#include "kernel/core/timer/m_timer_core.h"
#include "kernel/core/vfs/m_vfs.h"
#include "kernel/core/vfs/fd/m_vfs_fd.h"
#include "kernel/core/vfs/core/m_vfs_object.h"

#define LIBC_ERRNO_TLS_SLOT 0u
#define LIBC_EXIT_TLS_SLOT  1u
#define LIBC_ATEXIT_TLS_SLOT 2u

static int s_fallback_errno;

static int *libc_errno_ptr(void)
{
    job_ctx_t *ctx = jctx_current();
    if (ctx == NULL) {
        return &s_fallback_errno;
    }

    int *stored = (int *)jctx_tls_get(ctx, LIBC_ERRNO_TLS_SLOT);
    if (stored != NULL) {
        return stored;
    }

    int *value = (int *)m_job_alloc(ctx, sizeof(int));
    if (value == NULL) {
        return &s_fallback_errno;
    }
    *value = 0;
    (void)jctx_tls_set(ctx, LIBC_ERRNO_TLS_SLOT, value, NULL);
    return value;
}

int *m_libc___errno(void)
{
    return libc_errno_ptr();
}

static void libc_set_errno(int value)
{
    int *slot = libc_errno_ptr();
    if (slot != NULL) {
        *slot = value;
    }
}

static m_libc_exit_frame_t *libc_exit_frame_get(void)
{
    job_ctx_t *ctx = jctx_current();
    if (ctx == NULL) {
        return NULL;
    }
    return (m_libc_exit_frame_t *)jctx_tls_get(ctx, LIBC_EXIT_TLS_SLOT);
}

void m_libc_exit_frame_push(m_libc_exit_frame_t *frame)
{
    if (frame == NULL) {
        return;
    }

    job_ctx_t *ctx = jctx_current();
    if (ctx == NULL) {
        return;
    }

    frame->code = 0;
    frame->prev = jctx_tls_get(ctx, LIBC_EXIT_TLS_SLOT);
    (void)jctx_tls_set(ctx, LIBC_EXIT_TLS_SLOT, frame, NULL);
}

void m_libc_exit_frame_pop(m_libc_exit_frame_t *frame)
{
    if (frame == NULL) {
        return;
    }

    job_ctx_t *ctx = jctx_current();
    if (ctx == NULL) {
        return;
    }

    if (jctx_tls_get(ctx, LIBC_EXIT_TLS_SLOT) == frame) {
        (void)jctx_tls_set(ctx, LIBC_EXIT_TLS_SLOT, frame->prev, NULL);
    }
}

static void libc_exit_with_code(int code)
{
    m_libc_exit_frame_t *frame = libc_exit_frame_get();
    if (frame != NULL) {
        frame->code = code;
        longjmp(frame->env, 1);
    }

    m_arch_panic("libc exit without frame");
}

typedef enum {
    LIBC_EXITPROC_VOID = 0,
    LIBC_EXITPROC_CXA = 1,
} libc_exitproc_kind_t;

typedef struct {
    libc_exitproc_kind_t kind;
    void *dso;
    union {
        void (*fn_void)(void);
        void (*fn_cxa)(void *);
    } fn;
    void *arg;
} libc_exitproc_t;

typedef struct {
    size_t count;
    size_t capacity;
    libc_exitproc_t procs[];
} libc_atexit_state_t;

static libc_atexit_state_t *libc_atexit_get(void)
{
    job_ctx_t *ctx = jctx_current();
    if (ctx == NULL) {
        return NULL;
    }
    return (libc_atexit_state_t *)jctx_tls_get(ctx, LIBC_ATEXIT_TLS_SLOT);
}

static libc_atexit_state_t *libc_atexit_ensure(size_t min_capacity)
{
    job_ctx_t *ctx = jctx_current();
    if (ctx == NULL) {
        return NULL;
    }

    libc_atexit_state_t *state = libc_atexit_get();
    if (state != NULL && state->capacity >= min_capacity) {
        return state;
    }

    size_t new_cap = state ? state->capacity : 0;
    if (new_cap < 8) {
        new_cap = 8;
    }
    while (new_cap < min_capacity) {
        new_cap *= 2;
    }

    size_t bytes = sizeof(*state) + new_cap * sizeof(state->procs[0]);
    libc_atexit_state_t *next = (libc_atexit_state_t *)m_job_realloc(ctx, state, bytes);
    if (next == NULL) {
        return NULL;
    }
    if (state == NULL) {
        next->count = 0;
    }
    next->capacity = new_cap;
    (void)jctx_tls_set(ctx, LIBC_ATEXIT_TLS_SLOT, next, NULL);
    return next;
}

static void libc_run_exit_handlers(void)
{
    libc_atexit_state_t *state = libc_atexit_get();
    if (state == NULL) {
        return;
    }

    while (state->count > 0) {
        libc_exitproc_t proc = state->procs[state->count - 1];
        state->count--;
        if (proc.kind == LIBC_EXITPROC_VOID) {
            if (proc.fn.fn_void != NULL) {
                proc.fn.fn_void();
            }
        } else if (proc.kind == LIBC_EXITPROC_CXA) {
            if (proc.fn.fn_cxa != NULL) {
                proc.fn.fn_cxa(proc.arg);
            }
        }
    }
}

void m_libc_exit(int status)
{
    libc_run_exit_handlers();
    libc_exit_with_code(status);
}

void m_libc__exit(int status)
{
    libc_exit_with_code(status);
}

void m_libc_abort(void)
{
    libc_exit_with_code(134);
}

int m_libc_atexit(void (*fn)(void))
{
    if (fn == NULL) {
        libc_set_errno(EINVAL);
        return -1;
    }

    libc_atexit_state_t *state = libc_atexit_ensure(1);
    if (state == NULL) {
        libc_set_errno(ENOMEM);
        return -1;
    }
    if (state->count >= state->capacity) {
        state = libc_atexit_ensure(state->capacity + 1);
        if (state == NULL) {
            libc_set_errno(ENOMEM);
            return -1;
        }
    }

    state->procs[state->count++] = (libc_exitproc_t){
        .kind = LIBC_EXITPROC_VOID,
        .dso = NULL,
        .fn.fn_void = fn,
        .arg = NULL,
    };
    return 0;
}

int m_libc___cxa_atexit(void (*fn)(void *), void *arg, void *dso)
{
    if (fn == NULL) {
        libc_set_errno(EINVAL);
        return -1;
    }

    libc_atexit_state_t *state = libc_atexit_ensure(1);
    if (state == NULL) {
        libc_set_errno(ENOMEM);
        return -1;
    }
    if (state->count >= state->capacity) {
        state = libc_atexit_ensure(state->capacity + 1);
        if (state == NULL) {
            libc_set_errno(ENOMEM);
            return -1;
        }
    }

    state->procs[state->count++] = (libc_exitproc_t){
        .kind = LIBC_EXITPROC_CXA,
        .dso = dso,
        .fn.fn_cxa = fn,
        .arg = arg,
    };
    return 0;
}

void m_libc___cxa_finalize(void *dso)
{
    libc_atexit_state_t *state = libc_atexit_get();
    if (state == NULL) {
        return;
    }

    if (dso == NULL) {
        libc_run_exit_handlers();
        return;
    }

    for (size_t i = state->count; i > 0; --i) {
        libc_exitproc_t *proc = &state->procs[i - 1];
        if (proc->kind == LIBC_EXITPROC_CXA && proc->dso == dso) {
            libc_exitproc_t call = *proc;
            memmove(proc, proc + 1, (state->count - i) * sizeof(*proc));
            state->count--;
            if (call.fn.fn_cxa != NULL) {
                call.fn.fn_cxa(call.arg);
            }
        }
    }
}

static int libc_errno_from_vfs_error(m_vfs_error_t err)
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

static inline m_job_id_t libc_job_id(void)
{
    m_job_id_t id = jctx_current_job_id();
    return id;
}

static bool libc_build_absolute_path(const char *path, char *out, size_t capacity)
{
    if (path == NULL || out == NULL || capacity == 0) {
        return false;
    }

    if (path[0] == '/') {
        return m_vfs_path_normalize(path, out, capacity);
    }

    m_job_id_t job = libc_job_id();
    if (job == NULL) {
        return false;
    }

    char cwd[JOB_CTX_CWD_MAX_LEN] = {0};
    if (m_job_field_get(job, JOB_CTX_FIELD_CWD, cwd, sizeof(cwd)) != JOB_CTX_OK) {
        return false;
    }

    if (cwd[0] == '\0') {
        cwd[0] = '/';
        cwd[1] = '\0';
    }

    char combined[M_VFS_PATH_MAX_LEN] = {0};
    int needed = 0;
    if (cwd[1] == '\0') {
        needed = snprintf(combined, sizeof(combined), "/%s", path);
    } else {
        needed = snprintf(combined, sizeof(combined), "%s/%s", cwd, path);
    }
    if (needed < 0 || (size_t)needed >= sizeof(combined)) {
        return false;
    }

    return m_vfs_path_normalize(combined, out, capacity);
}

static ssize_t libc_console_write(const void *buffer, size_t size)
{
    if (buffer == NULL) {
        return -1;
    }

    const uint8_t *bytes = (const uint8_t *)buffer;
    for (size_t i = 0; i < size; ++i) {
        esp_rom_output_putc((char)bytes[i]);
    }
    return (ssize_t)size;
}

int m_libc_open(const char *path, int flags, ...)
{
    if (path == NULL) {
        libc_set_errno(EINVAL);
        return -1;
    }

    int fd = -1;
    m_vfs_error_t err = m_vfs_open(libc_job_id(), path, flags, &fd);
    if (err != M_VFS_ERR_OK) {
        if (err == M_VFS_ERR_BUSY && (flags & O_CREAT) && (flags & O_EXCL)) {
            libc_set_errno(EEXIST);
        } else {
            libc_set_errno(libc_errno_from_vfs_error(err));
        }
        return -1;
    }
    return fd;
}

int m_libc_close(int fd)
{
    if (fd >= 0 && fd <= 2) {
        return 0;
    }
    m_vfs_error_t err = m_vfs_close(libc_job_id(), fd);
    if (err != M_VFS_ERR_OK) {
        libc_set_errno(libc_errno_from_vfs_error(err));
        return -1;
    }
    return 0;
}

ssize_t m_libc_read(int fd, void *buffer, size_t size)
{
    if (fd == 0) {
        if (buffer == NULL) {
            libc_set_errno(EFAULT);
            return -1;
        }
        if (size == 0) {
            return 0;
        }

        uint8_t *out = (uint8_t *)buffer;
        size_t produced = 0;

        uint8_t c = 0;
        while (esp_rom_output_rx_one_char(&c) != 0) {
            vTaskDelay(1);
        }

        if (c == '\r') {
            c = '\n';
        }
        out[produced++] = c;

        while (produced < size) {
            if (esp_rom_output_rx_one_char(&c) != 0) {
                break;
            }
            if (c == '\r') {
                c = '\n';
            }
            out[produced++] = c;
        }
        return (ssize_t)produced;
    }

    if (fd >= 1 && fd <= 2) {
        libc_set_errno(EBADF);
        return -1;
    }

    size_t read_bytes = 0;
    m_vfs_error_t err = m_vfs_read(libc_job_id(), fd, buffer, size, &read_bytes);
    if (err != M_VFS_ERR_OK) {
        libc_set_errno(libc_errno_from_vfs_error(err));
        return -1;
    }
    return (ssize_t)read_bytes;
}

ssize_t m_libc_write(int fd, const void *buffer, size_t size)
{
    if (fd == 1 || fd == 2) {
        return libc_console_write(buffer, size);
    }
    if (fd == 0) {
        libc_set_errno(EBADF);
        return -1;
    }

    size_t written = 0;
    m_vfs_error_t err = m_vfs_write(libc_job_id(), fd, buffer, size, &written);
    if (err != M_VFS_ERR_OK) {
        libc_set_errno(libc_errno_from_vfs_error(err));
        return -1;
    }
    return (ssize_t)written;
}

static bool libc_file_getattr(m_vfs_file_t *file, m_vfs_stat_t *out)
{
    if (file == NULL || file->node == NULL || out == NULL) {
        return false;
    }
    m_vfs_node_t *node = file->node;
    if (node->fs_type == NULL || node->fs_type->ops == NULL || node->fs_type->ops->getattr == NULL) {
        return false;
    }
    return node->fs_type->ops->getattr(node, out) == M_VFS_ERR_OK;
}

static size_t libc_file_get_offset(m_vfs_file_t *file)
{
    if (file == NULL) {
        return 0;
    }
    portENTER_CRITICAL(&file->lock);
    size_t offset = file->offset;
    portEXIT_CRITICAL(&file->lock);
    return offset;
}

off_t m_libc_lseek(int fd, off_t offset, int whence)
{
    if (fd >= 0 && fd <= 2) {
        libc_set_errno(ESPIPE);
        return (off_t)-1;
    }

    if (offset < 0) {
        libc_set_errno(EINVAL);
        return (off_t)-1;
    }

    m_vfs_file_t *file = m_vfs_fd_lookup(libc_job_id(), fd);
    if (file == NULL) {
        libc_set_errno(EBADF);
        return (off_t)-1;
    }

    size_t base = 0;
    switch (whence) {
    case SEEK_SET:
        base = 0;
        break;
    case SEEK_CUR:
        base = libc_file_get_offset(file);
        break;
    case SEEK_END: {
        m_vfs_stat_t st = {0};
        if (!libc_file_getattr(file, &st)) {
            libc_set_errno(ENOTSUP);
            return (off_t)-1;
        }
        base = st.size;
        break;
    }
    default:
        libc_set_errno(EINVAL);
        return (off_t)-1;
    }

    size_t target = base + (size_t)offset;
    m_vfs_file_set_offset(file, target);
    return (off_t)target;
}

int m_libc_ioctl(int fd, unsigned long request, ...)
{
    void *arg = NULL;
    va_list ap;
    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);

    if (fd >= 0 && fd <= 2) {
        libc_set_errno(ENOTTY);
        return -1;
    }

    m_vfs_error_t err = m_vfs_ioctl(libc_job_id(), fd, request, arg);
    if (err != M_VFS_ERR_OK) {
        libc_set_errno(libc_errno_from_vfs_error(err));
        return -1;
    }
    return 0;
}

int m_libc_dup(int oldfd)
{
    if (oldfd >= 0 && oldfd <= 2) {
        return oldfd;
    }

    int fd = -1;
    m_vfs_error_t err = m_vfs_dup(libc_job_id(), oldfd, &fd);
    if (err != M_VFS_ERR_OK) {
        libc_set_errno(libc_errno_from_vfs_error(err));
        return -1;
    }
    return fd;
}

int m_libc_dup2(int oldfd, int newfd)
{
    if (newfd < 0) {
        libc_set_errno(EINVAL);
        return -1;
    }
    if (oldfd >= 0 && oldfd <= 2) {
        if (newfd == oldfd) {
            return newfd;
        }
        libc_set_errno(ENOTSUP);
        return -1;
    }

    m_vfs_error_t err = m_vfs_dup2(libc_job_id(), oldfd, newfd);
    if (err != M_VFS_ERR_OK) {
        libc_set_errno(libc_errno_from_vfs_error(err));
        return -1;
    }
    return newfd;
}

int m_libc_poll(void *fds, unsigned long nfds, int timeout_ms)
{
    if (fds == NULL && nfds != 0) {
        libc_set_errno(EINVAL);
        return -1;
    }

    struct pollfd *pfds = (struct pollfd *)fds;
    if (nfds == 0) {
        if (timeout_ms > 0) {
            vTaskDelay(m_timer_delta_to_ticks((uint64_t)timeout_ms * 1000u));
        }
        return 0;
    }

    m_vfs_pollfd_t *vfds = (m_vfs_pollfd_t *)m_libc_malloc(nfds * sizeof(*vfds));
    if (vfds == NULL) {
        libc_set_errno(ENOMEM);
        return -1;
    }

    for (unsigned long i = 0; i < nfds; ++i) {
        uint32_t events = 0;
        if (pfds[i].events & POLLIN) {
            events |= M_VFS_POLLIN;
        }
        if (pfds[i].events & POLLOUT) {
            events |= M_VFS_POLLOUT;
        }
        vfds[i].fd = pfds[i].fd;
        vfds[i].events = events;
        vfds[i].revents = 0;
        pfds[i].revents = 0;

        if (pfds[i].fd == 1 || pfds[i].fd == 2) {
            if (pfds[i].events & POLLOUT) {
                pfds[i].revents |= POLLOUT;
            }
        }
    }

    const m_timer_deadline_t *deadline = NULL;
    m_timer_deadline_t local_deadline;
    if (timeout_ms >= 0) {
        local_deadline = m_timer_deadline_from_relative((uint64_t)timeout_ms * 1000u);
        deadline = &local_deadline;
    }

    size_t ready = 0;
    m_vfs_error_t err = m_vfs_poll(libc_job_id(), vfds, nfds, deadline, &ready);
    if (err != M_VFS_ERR_OK) {
        m_libc_free(vfds);
        libc_set_errno(libc_errno_from_vfs_error(err));
        return -1;
    }

    int ready_count = 0;
    for (unsigned long i = 0; i < nfds; ++i) {
        uint16_t revents = 0;
        if (vfds[i].revents & M_VFS_POLLIN) {
            revents |= POLLIN;
        }
        if (vfds[i].revents & M_VFS_POLLOUT) {
            revents |= POLLOUT;
        }
        if (vfds[i].revents & M_VFS_POLLERR) {
            revents |= POLLERR;
        }
        if (vfds[i].revents & M_VFS_POLLHUP) {
            revents |= POLLHUP;
        }
        pfds[i].revents |= revents;
        if (pfds[i].revents != 0) {
            ++ready_count;
        }
    }
    m_libc_free(vfds);
    return ready_count;
}

int m_libc_isatty(int fd)
{
    if (fd >= 0 && fd <= 2) {
        return 1;
    }
    libc_set_errno(ENOTTY);
    return 0;
}

int m_libc_access(const char *path, int mode)
{
    (void)mode;
    struct stat st;
    return m_libc_stat(path, &st);
}

void *m_libc_malloc(size_t size)
{
    void *ptr = m_job_alloc(NULL, size);
    if (ptr == NULL) {
        libc_set_errno(ENOMEM);
    }
    return ptr;
}

void *m_libc_calloc(size_t nmemb, size_t size)
{
    void *ptr = m_job_calloc(NULL, nmemb, size);
    if (ptr == NULL) {
        libc_set_errno(ENOMEM);
    }
    return ptr;
}

void *m_libc_realloc(void *ptr, size_t size)
{
    void *out = m_job_realloc(NULL, ptr, size);
    if (out == NULL && size != 0) {
        libc_set_errno(ENOMEM);
    }
    return out;
}

void m_libc_free(void *ptr)
{
    m_job_free(NULL, ptr);
}

int m_libc_remove(const char *path)
{
    return m_libc_unlink(path);
}

int m_libc_unlink(const char *path)
{
    if (path == NULL) {
        libc_set_errno(EINVAL);
        return -1;
    }
    m_vfs_error_t err = m_vfs_unlink(libc_job_id(), path);
    if (err != M_VFS_ERR_OK) {
        libc_set_errno(libc_errno_from_vfs_error(err));
        return -1;
    }
    return 0;
}

int m_libc_mkdir(const char *path, mode_t mode)
{
    (void)mode;
    if (path == NULL) {
        libc_set_errno(EINVAL);
        return -1;
    }
    m_vfs_error_t err = m_vfs_mkdir(libc_job_id(), path, (uint32_t)mode);
    if (err != M_VFS_ERR_OK) {
        libc_set_errno(libc_errno_from_vfs_error(err));
        return -1;
    }
    return 0;
}

int m_libc_chdir(const char *path)
{
    if (path == NULL) {
        libc_set_errno(EINVAL);
        return -1;
    }
    m_vfs_error_t err = m_vfs_chdir(libc_job_id(), path);
    if (err != M_VFS_ERR_OK) {
        libc_set_errno(libc_errno_from_vfs_error(err));
        return -1;
    }
    return 0;
}

char *m_libc_getcwd(char *buffer, size_t size)
{
    if (buffer == NULL || size == 0) {
        libc_set_errno(EINVAL);
        return NULL;
    }
    m_vfs_error_t err = m_vfs_getcwd(libc_job_id(), buffer, size);
    if (err != M_VFS_ERR_OK) {
        libc_set_errno(libc_errno_from_vfs_error(err));
        return NULL;
    }
    return buffer;
}

static void libc_fill_posix_stat(const m_vfs_stat_t *in, struct stat *out)
{
    memset(out, 0, sizeof(*out));
    mode_t mode = (mode_t)in->mode;
    switch (in->type) {
    case M_VFS_NODE_TYPE_DIRECTORY:
        mode |= S_IFDIR;
        break;
    case M_VFS_NODE_TYPE_FILE:
        mode |= S_IFREG;
        break;
    case M_VFS_NODE_TYPE_DEVICE:
        mode |= S_IFCHR;
        break;
    default:
        break;
    }
    out->st_mode = mode;
    out->st_size = (off_t)in->size;
    out->st_mtime = (time_t)(in->mtime / 1000000u);
}

int m_libc_stat(const char *path, void *out_stat)
{
    if (path == NULL || out_stat == NULL) {
        libc_set_errno(EINVAL);
        return -1;
    }

    char normalized[M_VFS_PATH_MAX_LEN] = {0};
    if (!libc_build_absolute_path(path, normalized, sizeof(normalized))) {
        libc_set_errno(EINVAL);
        return -1;
    }

    m_vfs_path_t parsed;
    if (!m_vfs_path_parse(normalized, &parsed)) {
        libc_set_errno(EINVAL);
        return -1;
    }

    m_vfs_node_t *node = NULL;
    m_vfs_error_t err = m_vfs_path_resolve(libc_job_id(), &parsed, &node);
    if (err != M_VFS_ERR_OK || node == NULL) {
        libc_set_errno(libc_errno_from_vfs_error(err));
        return -1;
    }

    if (node->fs_type == NULL || node->fs_type->ops == NULL || node->fs_type->ops->getattr == NULL) {
        m_vfs_node_release(node);
        libc_set_errno(ENOTSUP);
        return -1;
    }

    m_vfs_stat_t st = {0};
    err = node->fs_type->ops->getattr(node, &st);
    m_vfs_node_release(node);
    if (err != M_VFS_ERR_OK) {
        libc_set_errno(libc_errno_from_vfs_error(err));
        return -1;
    }

    libc_fill_posix_stat(&st, (struct stat *)out_stat);
    return 0;
}

int m_libc_fstat(int fd, void *out_stat)
{
    if (out_stat == NULL) {
        libc_set_errno(EINVAL);
        return -1;
    }

    if (fd >= 0 && fd <= 2) {
        libc_set_errno(ENOTSUP);
        return -1;
    }

    m_vfs_file_t *file = m_vfs_fd_lookup(libc_job_id(), fd);
    if (file == NULL) {
        libc_set_errno(EBADF);
        return -1;
    }

    m_vfs_stat_t st = {0};
    if (!libc_file_getattr(file, &st)) {
        libc_set_errno(ENOTSUP);
        return -1;
    }

    libc_fill_posix_stat(&st, (struct stat *)out_stat);
    return 0;
}

typedef struct {
    int fd;
    struct dirent entry;
    bool eof;
} m_libc_dir_t;

void *m_libc_opendir(const char *path)
{
    if (path == NULL) {
        libc_set_errno(EINVAL);
        return NULL;
    }

    int fd = -1;
    m_vfs_error_t err = m_vfs_open(libc_job_id(), path, O_RDONLY, &fd);
    if (err != M_VFS_ERR_OK) {
        libc_set_errno(libc_errno_from_vfs_error(err));
        return NULL;
    }

    m_libc_dir_t *dir = (m_libc_dir_t *)m_libc_malloc(sizeof(*dir));
    if (dir == NULL) {
        (void)m_vfs_close(libc_job_id(), fd);
        libc_set_errno(ENOMEM);
        return NULL;
    }
    memset(dir, 0, sizeof(*dir));
    dir->fd = fd;
    dir->eof = false;
    return (void *)dir;
}

void *m_libc_readdir(void *dirp)
{
    if (dirp == NULL) {
        libc_set_errno(EINVAL);
        return NULL;
    }

    m_libc_dir_t *dir = (m_libc_dir_t *)dirp;
    if (dir->eof) {
        return NULL;
    }

    m_vfs_dirent_t ventry = {0};
    size_t populated = 0;
    m_vfs_error_t err = m_vfs_readdir(libc_job_id(), dir->fd, &ventry, 1, &populated);
    if (err != M_VFS_ERR_OK) {
        libc_set_errno(libc_errno_from_vfs_error(err));
        return NULL;
    }
    if (populated == 0) {
        dir->eof = true;
        return NULL;
    }

    memset(&dir->entry, 0, sizeof(dir->entry));
    strncpy(dir->entry.d_name, ventry.name, sizeof(dir->entry.d_name) - 1);
#ifdef DT_DIR
    if (ventry.type == M_VFS_NODE_TYPE_DIRECTORY) {
        dir->entry.d_type = DT_DIR;
    } else if (ventry.type == M_VFS_NODE_TYPE_FILE) {
        dir->entry.d_type = DT_REG;
    } else if (ventry.type == M_VFS_NODE_TYPE_DEVICE) {
        dir->entry.d_type = DT_CHR;
    } else {
        dir->entry.d_type = DT_UNKNOWN;
    }
#endif
    return (void *)&dir->entry;
}

int m_libc_closedir(void *dirp)
{
    if (dirp == NULL) {
        libc_set_errno(EINVAL);
        return -1;
    }

    m_libc_dir_t *dir = (m_libc_dir_t *)dirp;
    int fd = dir->fd;
    m_libc_free(dir);
    m_vfs_error_t err = m_vfs_close(libc_job_id(), fd);
    if (err != M_VFS_ERR_OK) {
        libc_set_errno(libc_errno_from_vfs_error(err));
        return -1;
    }
    return 0;
}

void m_libc_rewinddir(void *dirp)
{
    if (dirp == NULL) {
        return;
    }

    m_libc_dir_t *dir = (m_libc_dir_t *)dirp;
    dir->eof = false;
    (void)m_libc_lseek(dir->fd, 0, SEEK_SET);
}

int m_libc_getpid(void)
{
    return (int)(uintptr_t)libc_job_id();
}

int m_libc_getppid(void)
{
    m_job_id_t parent = NULL;
    (void)m_job_field_get(libc_job_id(), JOB_CTX_FIELD_PARENT_JOB_ID, &parent, sizeof(parent));
    return (int)(uintptr_t)parent;
}

unsigned int m_libc_getuid(void)
{
    uint32_t uid = 0;
    (void)m_job_field_get(libc_job_id(), JOB_CTX_FIELD_UID, &uid, sizeof(uid));
    return (unsigned int)uid;
}

unsigned int m_libc_getgid(void)
{
    uint32_t gid = 0;
    (void)m_job_field_get(libc_job_id(), JOB_CTX_FIELD_GID, &gid, sizeof(gid));
    return (unsigned int)gid;
}

unsigned int m_libc_geteuid(void)
{
    uint32_t uid = 0;
    (void)m_job_field_get(libc_job_id(), JOB_CTX_FIELD_EUID, &uid, sizeof(uid));
    return (unsigned int)uid;
}

unsigned int m_libc_getegid(void)
{
    uint32_t gid = 0;
    (void)m_job_field_get(libc_job_id(), JOB_CTX_FIELD_EGID, &gid, sizeof(gid));
    return (unsigned int)gid;
}

int m_libc_clock_gettime(int clock_id, void *tp)
{
    if (tp == NULL) {
        libc_set_errno(EINVAL);
        return -1;
    }

    struct timespec *spec = (struct timespec *)tp;
    if (clock_id != CLOCK_MONOTONIC && clock_id != CLOCK_REALTIME) {
        libc_set_errno(EINVAL);
        return -1;
    }

    uint64_t us = m_timer_get_monotonic();
    spec->tv_sec = (time_t)(us / 1000000u);
    spec->tv_nsec = (long)((us % 1000000u) * 1000u);
    return 0;
}

int m_libc_gettimeofday(void *tv, void *tz)
{
    (void)tz;
    if (tv == NULL) {
        libc_set_errno(EINVAL);
        return -1;
    }

    struct timeval *out = (struct timeval *)tv;
    uint64_t us = m_timer_get_monotonic();
    out->tv_sec = (time_t)(us / 1000000u);
    out->tv_usec = (suseconds_t)(us % 1000000u);
    return 0;
}

time_t m_libc_time(time_t *tloc)
{
    time_t now = (time_t)(m_timer_get_monotonic() / 1000000u);
    if (tloc != NULL) {
        *tloc = now;
    }
    return now;
}

unsigned int m_libc_sleep(unsigned int seconds)
{
    if (seconds == 0) {
        return 0;
    }
    vTaskDelay(m_timer_delta_to_ticks((uint64_t)seconds * 1000000u));
    return 0;
}

int m_libc_usleep(unsigned int usec)
{
    if (usec == 0) {
        return 0;
    }
    vTaskDelay(m_timer_delta_to_ticks((uint64_t)usec));
    return 0;
}

int m_libc_nanosleep(const void *req, void *rem)
{
    (void)rem;
    if (req == NULL) {
        libc_set_errno(EINVAL);
        return -1;
    }

    const struct timespec *ts = (const struct timespec *)req;
    if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000L) {
        libc_set_errno(EINVAL);
        return -1;
    }

    uint64_t us = (uint64_t)ts->tv_sec * 1000000u + (uint64_t)ts->tv_nsec / 1000u;
    if (us > 0) {
        vTaskDelay(m_timer_delta_to_ticks(us));
    }
    return 0;
}

static inline void libc_reent_set_errno(struct _reent *r, int value)
{
    libc_set_errno(value);
    if (r != NULL) {
        r->_errno = value;
    }
}

void *m_libc_malloc_r(struct _reent *r, size_t size)
{
    void *ptr = m_libc_malloc(size);
    if (ptr == NULL) {
        libc_reent_set_errno(r, *m_libc___errno());
    }
    return ptr;
}

void *m_libc_calloc_r(struct _reent *r, size_t nmemb, size_t size)
{
    void *ptr = m_libc_calloc(nmemb, size);
    if (ptr == NULL) {
        libc_reent_set_errno(r, *m_libc___errno());
    }
    return ptr;
}

void *m_libc_realloc_r(struct _reent *r, void *ptr, size_t size)
{
    void *out = m_libc_realloc(ptr, size);
    if (out == NULL && size != 0) {
        libc_reent_set_errno(r, *m_libc___errno());
    }
    return out;
}

void m_libc_free_r(struct _reent *r, void *ptr)
{
    (void)r;
    m_libc_free(ptr);
}

int m_libc_open_r(struct _reent *r, const char *file, int flags, int mode)
{
    int fd = m_libc_open(file, flags, mode);
    if (fd < 0) {
        libc_reent_set_errno(r, *m_libc___errno());
    }
    return fd;
}

int m_libc_close_r(struct _reent *r, int fd)
{
    int rc = m_libc_close(fd);
    if (rc < 0) {
        libc_reent_set_errno(r, *m_libc___errno());
    }
    return rc;
}

_ssize_t m_libc_read_r(struct _reent *r, int fd, void *buf, size_t cnt)
{
    ssize_t rc = m_libc_read(fd, buf, cnt);
    if (rc < 0) {
        libc_reent_set_errno(r, *m_libc___errno());
    }
    return (_ssize_t)rc;
}

_ssize_t m_libc_write_r(struct _reent *r, int fd, const void *buf, size_t cnt)
{
    ssize_t rc = m_libc_write(fd, buf, cnt);
    if (rc < 0) {
        libc_reent_set_errno(r, *m_libc___errno());
    }
    return (_ssize_t)rc;
}

_off_t m_libc_lseek_r(struct _reent *r, int fd, _off_t pos, int whence)
{
    off_t rc = m_libc_lseek(fd, (off_t)pos, whence);
    if (rc < 0) {
        libc_reent_set_errno(r, *m_libc___errno());
    }
    return (_off_t)rc;
}

int m_libc_fstat_r(struct _reent *r, int fd, struct stat *st)
{
    int rc = m_libc_fstat(fd, st);
    if (rc < 0) {
        libc_reent_set_errno(r, *m_libc___errno());
    }
    return rc;
}

int m_libc_stat_r(struct _reent *r, const char *file, struct stat *st)
{
    int rc = m_libc_stat(file, st);
    if (rc < 0) {
        libc_reent_set_errno(r, *m_libc___errno());
    }
    return rc;
}

int m_libc_isatty_r(struct _reent *r, int fd)
{
    int rc = m_libc_isatty(fd);
    if (!rc) {
        libc_reent_set_errno(r, *m_libc___errno());
    }
    return rc;
}

int m_libc_unlink_r(struct _reent *r, const char *file)
{
    int rc = m_libc_unlink(file);
    if (rc < 0) {
        libc_reent_set_errno(r, *m_libc___errno());
    }
    return rc;
}

int m_libc_mkdir_r(struct _reent *r, const char *path, int mode)
{
    int rc = m_libc_mkdir(path, (mode_t)mode);
    if (rc < 0) {
        libc_reent_set_errno(r, *m_libc___errno());
    }
    return rc;
}

int m_libc_chdir_r(struct _reent *r, const char *path)
{
    int rc = m_libc_chdir(path);
    if (rc < 0) {
        libc_reent_set_errno(r, *m_libc___errno());
    }
    return rc;
}

char *m_libc_getcwd_r(struct _reent *r, char *buf, size_t size)
{
    char *rc = m_libc_getcwd(buf, size);
    if (rc == NULL) {
        libc_reent_set_errno(r, *m_libc___errno());
    }
    return rc;
}

int m_libc_gettimeofday_r(struct _reent *r, struct timeval *tv, void *tzp)
{
    int rc = m_libc_gettimeofday(tv, tzp);
    if (rc < 0) {
        libc_reent_set_errno(r, *m_libc___errno());
    }
    return rc;
}

clock_t m_libc_times_r(struct _reent *r, struct tms *buf)
{
    (void)r;
    uint64_t us = m_timer_get_monotonic();
    clock_t ticks = (clock_t)(us * (uint64_t)CLOCKS_PER_SEC / 1000000u);
    if (buf != NULL) {
        buf->tms_utime = ticks;
        buf->tms_stime = 0;
        buf->tms_cutime = 0;
        buf->tms_cstime = 0;
    }
    return ticks;
}

void *m_libc_sbrk_r(struct _reent *r, ptrdiff_t incr)
{
    (void)incr;
    libc_reent_set_errno(r, ENOMEM);
    return (void *)-1;
}

int m_libc_kill_r(struct _reent *r, int pid, int sig)
{
    (void)pid;
    (void)sig;
    libc_reent_set_errno(r, ENOSYS);
    return -1;
}

int m_libc_getpid_r(struct _reent *r)
{
    (void)r;
    return m_libc_getpid();
}

int m_libc_rename_r(struct _reent *r, const char *old, const char *newpath)
{
    (void)old;
    (void)newpath;
    libc_reent_set_errno(r, ENOSYS);
    return -1;
}

int m_libc_link_r(struct _reent *r, const char *old, const char *newpath)
{
    (void)old;
    (void)newpath;
    libc_reent_set_errno(r, ENOSYS);
    return -1;
}

int m_libc_rmdir_r(struct _reent *r, const char *path)
{
    (void)path;
    libc_reent_set_errno(r, ENOSYS);
    return -1;
}
