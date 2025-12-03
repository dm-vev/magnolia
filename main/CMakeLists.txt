if(CONFIG_IDF_TARGET_ARCH_XTENSA)
    if(CONFIG_IDF_TARGET_ESP32)
        set(ARCH_START "kernel/arch/xtensa/esp32/start.c")
    elseif(CONFIG_IDF_TARGET_ESP32S2)
        set(ARCH_START "kernel/arch/xtensa/esp32s2/start.c")
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
    elseif(CONFIG_IDF_TARGET_ESP32P4)
        set(ARCH_START "kernel/arch/riscv/esp32p4/start.c")
    endif()
endif()

set(APP_SRCS
    "main.c"
    "kernel/arch/m_hw_init.c"
    ${ARCH_START}
    "kernel/core/timer/m_timer.c"
    "kernel/core/sched/m_sched.c"
)

if(CONFIG_MAGNOLIA_JOB_ENABLED)
    list(APPEND APP_SRCS
        "kernel/core/job/m_job.c"
        "kernel/core/job/m_job_core.c"
        "kernel/core/job/m_job_queue.c"
        "kernel/core/job/m_job_worker.c"
        "kernel/core/job/m_job_result.c"
        "kernel/core/job/m_job_future.c"
        "kernel/core/job/m_job_wait.c"
        "kernel/core/job/m_job_diag.c"
        "kernel/core/job/jctx.c"
        "lib/libexec/job_ctx.c"
    )
endif()

list(APPEND APP_SRCS
    "kernel/core/ipc/ipc.c"
    "kernel/core/ipc/ipc_core.c"
    "kernel/core/ipc/ipc_scheduler_bridge.c"
    "kernel/core/ipc/ipc_signal.c"
    "kernel/core/ipc/ipc_shm.c"
    "kernel/core/ipc/ipc_diag.c"
    "kernel/core/ipc/ipc_channel.c"
    "kernel/core/ipc/ipc_event_flags.c"
)

if(CONFIG_MAGNOLIA_IPC_SELFTESTS)
    list(APPEND APP_SRCS "kernel/core/ipc/tests/ipc_signal_tests.c")
    list(APPEND APP_SRCS "kernel/core/ipc/tests/ipc_channel_tests.c")
    list(APPEND APP_SRCS "kernel/core/ipc/tests/ipc_event_flags_tests.c")
    list(APPEND APP_SRCS "kernel/core/ipc/tests/ipc_shm_tests.c")
endif()

list(APPEND APP_SRCS
    "kernel/core/sched/tests/m_sched_tests.c"
)

if(CONFIG_MAGNOLIA_JOB_ENABLED AND CONFIG_MAGNOLIA_JOB_SELFTESTS)
    list(APPEND APP_SRCS "kernel/core/job/tests/m_job_tests.c")
endif()
idf_component_register(
    SRCS ${APP_SRCS}
    INCLUDE_DIRS
        "."
        "kernel/core/timer"
        "kernel/core/sched"
        "kernel/core/job"
        "kernel/core/ipc"
        "lib/libexec"
)
