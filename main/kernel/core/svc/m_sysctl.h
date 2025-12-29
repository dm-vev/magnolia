#ifndef MAGNOLIA_CORE_SVC_M_SYSCTL_H
#define MAGNOLIA_CORE_SVC_M_SYSCTL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*m_sysctl_list_cb)(const char *key,
                                const char *value,
                                void *ctx);

int m_sysctl_get(const char *key, char *out_value, size_t value_size);
int m_sysctl_set(const char *key, const char *value);
int m_sysctl_list(const char *prefix, m_sysctl_list_cb cb, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_CORE_SVC_M_SYSCTL_H */
