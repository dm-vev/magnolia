#include "freertos/portmacro.h"

#include "kernel/core/vfs/core/m_vfs_test.h"

static portMUX_TYPE g_vfs_test_lock =
        (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
static bool g_vfs_error_injection_enabled;
static m_vfs_error_t g_vfs_error_injection_code = M_VFS_ERR_BUSY;

void
m_vfs_test_set_error_injection(bool enabled, m_vfs_error_t code)
{
    portENTER_CRITICAL(&g_vfs_test_lock);
    g_vfs_error_injection_enabled = enabled;
    g_vfs_error_injection_code = code;
    portEXIT_CRITICAL(&g_vfs_test_lock);
}

bool
m_vfs_test_error_injection_enabled(void)
{
    bool enabled;
    portENTER_CRITICAL(&g_vfs_test_lock);
    enabled = g_vfs_error_injection_enabled;
    portEXIT_CRITICAL(&g_vfs_test_lock);
    return enabled;
}

m_vfs_error_t
m_vfs_test_error_injection_code(void)
{
    m_vfs_error_t code;
    portENTER_CRITICAL(&g_vfs_test_lock);
    code = g_vfs_error_injection_code;
    portEXIT_CRITICAL(&g_vfs_test_lock);
    return code;
}
