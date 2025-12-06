set(ARCH_SRCS)

if(CONFIG_IDF_TARGET_ARCH_XTENSA)
    if(CONFIG_IDF_TARGET_ESP32)
        set(ARCH_SRCS
            "kernel/arch/xtensa/esp32/start.c"
            "kernel/arch/xtensa/esp32/cpu.c"
            "kernel/arch/xtensa/esp32/memory.c"
            "kernel/arch/xtensa/esp32/power.c"
        )
    elseif(CONFIG_IDF_TARGET_ESP32S2)
        set(ARCH_SRCS
            "kernel/arch/xtensa/esp32s2/start.c"
            "kernel/arch/xtensa/esp32s2/cpu.c"
            "kernel/arch/xtensa/esp32s2/memory.c"
            "kernel/arch/xtensa/esp32s2/power.c"
        )
    elseif(CONFIG_IDF_TARGET_ESP32S3)
        set(ARCH_SRCS
            "kernel/arch/xtensa/esp32s3/start.c"
            "kernel/arch/xtensa/esp32s3/cpu.c"
            "kernel/arch/xtensa/esp32s3/memory.c"
            "kernel/arch/xtensa/esp32s3/power.c"
        )
    endif()
elseif(CONFIG_IDF_TARGET_ARCH_RISCV)
    if(CONFIG_IDF_TARGET_ESP32C3)
        set(ARCH_SRCS
            "kernel/arch/riscv/esp32c3/start.c"
            "kernel/arch/riscv/esp32c3/cpu.c"
            "kernel/arch/riscv/esp32c3/memory.c"
            "kernel/arch/riscv/esp32c3/power.c"
        )
    elseif(CONFIG_IDF_TARGET_ESP32C6)
        set(ARCH_SRCS
            "kernel/arch/riscv/esp32c6/start.c"
            "kernel/arch/riscv/esp32c6/cpu.c"
            "kernel/arch/riscv/esp32c6/memory.c"
            "kernel/arch/riscv/esp32c6/power.c"
        )
    elseif(CONFIG_IDF_TARGET_ESP32H2)
        set(ARCH_SRCS
            "kernel/arch/riscv/esp32h2/start.c"
            "kernel/arch/riscv/esp32h2/cpu.c"
            "kernel/arch/riscv/esp32h2/memory.c"
            "kernel/arch/riscv/esp32h2/power.c"
        )
    elseif(CONFIG_IDF_TARGET_ESP32P4)
        set(ARCH_SRCS
            "kernel/arch/riscv/esp32p4/start.c"
            "kernel/arch/riscv/esp32p4/cpu.c"
            "kernel/arch/riscv/esp32p4/memory.c"
            "kernel/arch/riscv/esp32p4/power.c"
        )
    endif()
endif()

set(APP_SRCS
    "main.c"
    "kernel/arch/m_hw_init.c"
    ${ARCH_SRCS}
    "kernel/core/timer/m_timer_core.c"
    "kernel/core/timer/m_timer_deadline.c"
    "kernel/core/timer/m_timer_queue.c"
    "kernel/core/timer/m_timer_diag.c"
    "kernel/core/sched/m_sched_core.c"
    "kernel/core/sched/m_sched_wait.c"
    "kernel/core/sched/m_sched_sleep.c"
    "kernel/core/sched/m_sched_worker.c"
    "kernel/core/sched/m_sched_diag.c"
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
        "kernel/core/job/m_job_event.c"
        "lib/libexec/job_ctx.c"
    )
endif()

if(CONFIG_MAGNOLIA_VFS_ENABLED)
    list(APPEND APP_SRCS
        "kernel/core/vfs/core/m_vfs_registry.c"
        "kernel/core/vfs/core/m_vfs_jobcwd.c"
        "kernel/core/vfs/core/m_vfs_object.c"
        "kernel/core/vfs/core/m_vfs_core.c"
        "kernel/core/vfs/cache/m_vfs_read_cache.c"
        "kernel/core/vfs/core/m_vfs_test.c"
        "kernel/core/vfs/core/m_vfs_errno.c"
        "kernel/core/vfs/path/m_vfs_path.c"
        "kernel/core/vfs/fd/m_vfs_fd.c"
        "kernel/core/vfs/diag/m_vfs_diag.c"
    )
    if(CONFIG_MAGNOLIA_VFS_SELFTESTS)
        list(APPEND APP_SRCS "kernel/core/vfs/core/m_vfs_selftests.c")
    endif()
    if(CONFIG_MAGNOLIA_RAMFS_ENABLED)
        list(APPEND APP_SRCS
            "kernel/core/vfs/ramfs/ramfs.c"
        )
    endif()
    if(CONFIG_MAGNOLIA_VFS_DEVFS)
        list(APPEND APP_SRCS
            "kernel/vfs/fs/devfs/devfs.c"
            "kernel/vfs/fs/devfs/devfs_stream.c"
            "kernel/vfs/fs/devfs/devfs_stream_devices.c"
        )
        if(CONFIG_MAGNOLIA_DEVFS_SELFTESTS)
            list(APPEND APP_SRCS
                "kernel/vfs/fs/devfs/devfs_tests.c"
            )
        endif()
    endif()
    if(CONFIG_MAGNOLIA_LITTLEFS_ENABLED)
        list(APPEND APP_SRCS
            "kernel/vfs/fs/littlefs/lfs.c"
            "kernel/vfs/fs/littlefs/lfs_util.c"
            "kernel/vfs/fs/littlefs/littlefs_fs.c"
            "kernel/vfs/fs/littlefs/lfs_backend_flash.c"
        )
    endif()
endif()

if(CONFIG_MAGNOLIA_ALLOC_ENABLED)
    list(APPEND APP_SRCS
        "kernel/core/memory/m_alloc.c"
    )
endif()

if(CONFIG_MAGNOLIA_IPC_ENABLED)
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
endif()

list(APPEND APP_SRCS
    "kernel/core/sched/tests/m_sched_tests.c"
)

list(APPEND APP_SRCS
    "kernel/core/timer/tests/m_timer_tests.c"
)

if(CONFIG_MAGNOLIA_JOB_ENABLED AND CONFIG_MAGNOLIA_JOB_SELFTESTS)
    list(APPEND APP_SRCS "kernel/core/job/tests/m_job_tests.c")
endif()

if(CONFIG_MAGNOLIA_ALLOC_ENABLED AND CONFIG_MAGNOLIA_ALLOC_SELFTESTS)
    list(APPEND APP_SRCS "kernel/core/memory/tests/m_alloc_tests.c")
endif()
idf_component_register(
    SRCS ${APP_SRCS}
    INCLUDE_DIRS
        "."
        "kernel/core/timer"
        "kernel/core/sched"
        "kernel/core/job"
        "kernel/core/ipc"
        "kernel/core/memory"
        "kernel/core/vfs"
        "kernel/vfs"
        "lib"
        "lib/libexec"
)
