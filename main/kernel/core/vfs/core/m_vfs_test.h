#ifndef MAGNOLIA_VFS_M_VFS_TEST_H
#define MAGNOLIA_VFS_M_VFS_TEST_H

#include <stdbool.h>

#include "kernel/core/vfs/m_vfs_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void m_vfs_test_set_error_injection(bool enabled,
                                    m_vfs_error_t code);
bool m_vfs_test_error_injection_enabled(void);
m_vfs_error_t m_vfs_test_error_injection_code(void);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_VFS_M_VFS_TEST_H */
