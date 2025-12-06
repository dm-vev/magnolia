#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/portmacro.h"

#include "kernel/core/vfs/core/m_vfs_object.h"
#include "kernel/core/vfs/core/m_vfs_registry.h"
#include "kernel/core/vfs/m_vfs.h"
#include "kernel/core/vfs/core/m_vfs_errno.h"

static size_t
_m_vfs_segment_count(const char *path)
{
    size_t count = 0;
    const char *cursor = path;

    while (*cursor != '\0') {
        while (*cursor == '/') {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        ++count;
        while (*cursor != '\0' && *cursor != '/') {
            ++cursor;
        }
    }

    return count;
}

bool
m_vfs_path_normalize(const char *path, char *out, size_t capacity)
{
    if (path == NULL || out == NULL || capacity == 0) {
        return false;
    }

    typedef struct {
        const char *start;
        size_t length;
        bool is_dotdot;
    } segment_desc_t;

    segment_desc_t segments[M_VFS_PATH_SEGMENT_MAX];
    size_t segment_count = 0;
    size_t actual_count = 0;
    bool absolute = (path[0] == '/');

    const char *cursor = path;
    while (*cursor == '/') {
        ++cursor;
    }

    while (*cursor != '\0') {
        const char *segment_start = cursor;
        size_t segment_length = 0;
        while (*cursor != '\0' && *cursor != '/') {
            ++segment_length;
            ++cursor;
        }

        if (segment_length == 0) {
            while (*cursor == '/') {
                ++cursor;
            }
            continue;
        }

        if (segment_length >= M_VFS_NAME_MAX_LEN) {
            return false;
        }

        if (segment_length == 1 && segment_start[0] == '.') {
            // Skip current directory markers.
        } else if (segment_length == 2 && segment_start[0] == '.' &&
                       segment_start[1] == '.') {
            if (absolute) {
                if (segment_count > 0) {
                    if (!segments[segment_count - 1].is_dotdot) {
                        --actual_count;
                    }
                    --segment_count;
                }
            } else if (actual_count > 0) {
                while (segment_count > 0) {
                    --segment_count;
                    if (!segments[segment_count].is_dotdot) {
                        --actual_count;
                        break;
                    }
                }
            } else {
                if (segment_count >= M_VFS_PATH_SEGMENT_MAX) {
                    return false;
                }
                segments[segment_count++] = (segment_desc_t){
                    .start = segment_start,
                    .length = segment_length,
                    .is_dotdot = true,
                };
            }
        } else {
            if (segment_count >= M_VFS_PATH_SEGMENT_MAX) {
                return false;
            }
            segments[segment_count++] = (segment_desc_t){
                .start = segment_start,
                .length = segment_length,
                .is_dotdot = false,
            };
            ++actual_count;
        }

        while (*cursor == '/') {
            ++cursor;
        }
    }

    size_t write_pos = 0;
    if (absolute) {
        if (capacity == 0) {
            return false;
        }
        out[write_pos++] = '/';
    }

    for (size_t i = 0; i < segment_count; ++i) {
        if (write_pos != 0 && out[write_pos - 1] != '/') {
            if (write_pos + 1 >= capacity) {
                return false;
            }
            out[write_pos++] = '/';
        }

        if (write_pos + segments[i].length >= capacity) {
            return false;
        }
        memcpy(out + write_pos, segments[i].start, segments[i].length);
        write_pos += segments[i].length;
    }

    if (!absolute && segment_count == 0) {
        if (capacity < 2) {
            return false;
        }
        out[0] = '.';
        write_pos = 1;
    }

    if (write_pos >= capacity) {
        return false;
    }

    out[write_pos] = '\0';
    return true;
}

bool
m_vfs_path_parse(const char *path, m_vfs_path_t *result)
{
    if (path == NULL || result == NULL) {
        return false;
    }

    if (!m_vfs_path_normalize(path, result->normalized, sizeof(result->normalized))) {
        return false;
    }

    result->segment_count = 0;
    const char *cursor = result->normalized;
    while (*cursor != '\0' && *cursor == '/') {
        ++cursor;
    }

    while (*cursor != '\0' && result->segment_count < M_VFS_PATH_SEGMENT_MAX) {
        const char *segment_start = cursor;
        size_t length = 0;
        while (*cursor != '\0' && *cursor != '/') {
            ++length;
            ++cursor;
        }

        if (length > 0) {
            result->segments[result->segment_count].name = segment_start;
            result->segments[result->segment_count].length = length;
            ++result->segment_count;
        }

        while (*cursor == '/') {
            ++cursor;
        }
    }

    return true;
}

static bool
_m_vfs_segment_is_dot(const m_vfs_path_segment_t *segment)
{
    return segment->length == 1 && segment->name[0] == '.';
}

static bool
_m_vfs_segment_is_dotdot(const m_vfs_path_segment_t *segment)
{
    return segment->length == 2 && segment->name[0] == '.' &&
           segment->name[1] == '.';
}

static bool
_m_vfs_copy_segment(const m_vfs_path_segment_t *segment,
                     char *out,
                     size_t len)
{
    if (segment == NULL || out == NULL || segment->length >= len) {
        return false;
    }

    memcpy(out, segment->name, segment->length);
    out[segment->length] = '\0';
    return true;
}

m_vfs_error_t
m_vfs_path_resolve(m_job_id_t job,
                   const m_vfs_path_t *path,
                   m_vfs_node_t **out_node)
{
    if (path == NULL || out_node == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    *out_node = NULL;
    m_vfs_mount_t *mount = m_vfs_registry_mount_best(path, NULL);
    if (mount == NULL) {
        return M_VFS_ERR_NOT_FOUND;
    }

    portENTER_CRITICAL(&mount->lock);
    m_vfs_node_t *current = mount->root;
    if (current != NULL) {
        m_vfs_node_acquire(current);
    }
    portEXIT_CRITICAL(&mount->lock);

    if (current == NULL) {
        return M_VFS_ERR_NOT_SUPPORTED;
    }

    size_t mount_segments = _m_vfs_segment_count(mount->target);
    for (size_t i = mount_segments; i < path->segment_count; ++i) {
        const m_vfs_path_segment_t *segment = &path->segments[i];
        if (_m_vfs_segment_is_dot(segment)) {
            continue;
        }
        if (_m_vfs_segment_is_dotdot(segment)) {
            if (current->parent != NULL) {
                m_vfs_node_t *parent = current->parent;
                m_vfs_node_acquire(parent);
                m_vfs_node_release(current);
                current = parent;
            }
            continue;
        }

        if (mount->fs_type == NULL || mount->fs_type->ops == NULL ||
                (mount->fs_type->ops->lookup == NULL &&
                 mount->fs_type->ops->lookup_errno == NULL)) {
            m_vfs_node_release(current);
            return M_VFS_ERR_NOT_SUPPORTED;
        }

        char lookup_name[M_VFS_NAME_MAX_LEN];
        if (!_m_vfs_copy_segment(segment, lookup_name, sizeof(lookup_name))) {
            m_vfs_node_release(current);
            return M_VFS_ERR_INVALID_PATH;
        }

        m_vfs_node_t *next = NULL;
        m_vfs_error_t err = M_VFS_ERR_NOT_SUPPORTED;
        if (mount->fs_type->ops->lookup_errno != NULL) {
            err = m_vfs_from_errno(mount->fs_type->ops->lookup_errno(mount,
                                                                     current,
                                                                     lookup_name,
                                                                     &next));
        } else {
            err = mount->fs_type->ops->lookup(mount,
                                              current,
                                              lookup_name,
                                              &next);
        }
        if (err != M_VFS_ERR_OK) {
            m_vfs_node_release(current);
            return err;
        }

        if (next == NULL) {
            m_vfs_node_release(current);
            return M_VFS_ERR_NOT_FOUND;
        }

        m_vfs_node_release(current);
        current = next;
    }

    *out_node = current;
    return M_VFS_ERR_OK;
}
