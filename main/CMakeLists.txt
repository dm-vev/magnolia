if(CONFIG_IDF_TARGET_ARCH_XTENSA)
    if(CONFIG_IDF_TARGET_ESP32)
        set(ARCH_START "kernel/arch/xtensa/esp32/start.c")
    elseif(CONFIG_IDF_TARGET_ESP32S3)
        set(ARCH_START "kernel/arch/xtensa/esp32s3/start.c")
    endif()
elseif(CONFIG_IDF_TARGET_ARCH_RISCV)
    if(CONFIG_IDF_TARGET_ESP32C3)
        set(ARCH_START "kernel/arch/riscv/esp32c3/start.c")
    elseif(CONFIG_IDF_TARGET_ESP32C6)
        set(ARCH_START "kernel/arch/riscv/esp32c6/start.c")
    elseif(CONFIG_IDF_TARGET_ESP32H2)
        set(ARCH_START "kernel/arch/riscv/esp32h2/start.c")
    endif()
endif()

set(APP_SRCS
    "main.c"
    ${ARCH_START}
    "kernel/core/timer/m_timer.c"
    "kernel/core/sched/m_sched.c"
    "kernel/core/job/m_job.c"
    "kernel/core/ipc/ipc.c"
    "kernel/core/ipc/ipc_core.c"
    "kernel/core/ipc/ipc_scheduler_bridge.c"
    "kernel/core/ipc/ipc_signal.c"
    "kernel/core/ipc/ipc_diag.c"
)

if(CONFIG_MAGNOLIA_IPC_SELFTESTS)
    list(APPEND APP_SRCS "kernel/core/ipc/tests/ipc_signal_tests.c")
endif()

idf_component_register(
    SRCS ${APP_SRCS}
    INCLUDE_DIRS
        "."
        "kernel/core/timer"
        "kernel/core/sched"
        "kernel/core/job"
        "kernel/core/ipc"
)
