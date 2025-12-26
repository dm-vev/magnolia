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
    # Always compile ramfs to provide stubs when disabled.
    list(APPEND APP_SRCS
        "kernel/core/vfs/ramfs/ramfs.c"
    )
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
        if(CONFIG_MAGNOLIA_VFS_LITTLEFS_SELFTESTS)
            list(APPEND APP_SRCS
                "kernel/vfs/fs/littlefs/littlefs_tests.c"
            )
        endif()
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

if(CONFIG_MAGNOLIA_ELF_ENABLED)
    list(APPEND APP_SRCS
        "kernel/core/elf/m_elf_loader.c"
        "kernel/core/elf/m_elf_platform.c"
        "kernel/core/elf/m_elf_symbol.c"
        "kernel/core/elf/m_elf_app_api.c"
        "kernel/core/libc/m_libc_compat.c"
    )

    if(CONFIG_IDF_TARGET_ARCH_XTENSA)
        list(APPEND APP_SRCS "kernel/arch/elf/m_elf_xtensa.c")
    elseif(CONFIG_IDF_TARGET_ARCH_RISCV)
        list(APPEND APP_SRCS "kernel/arch/elf/m_elf_riscv.c")
    endif()

    if(CONFIG_MAGNOLIA_ELF_SELFTESTS)
        list(APPEND APP_SRCS "kernel/core/elf/tests/m_elf_tests.c")

        if(CONFIG_IDF_TARGET_ARCH_XTENSA)
            set(KERNEL_TEST_ELF "kernel/core/elf/tests/kernel_test_xtensa.elf")
        elseif(CONFIG_IDF_TARGET_ARCH_RISCV)
            set(KERNEL_TEST_ELF "kernel/core/elf/tests/kernel_test_riscv.elf")
        endif()

        if(KERNEL_TEST_ELF AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/${KERNEL_TEST_ELF}")
            list(APPEND APP_EMBED_TXTFILES "${KERNEL_TEST_ELF}")
        endif()
    endif()
endif()
idf_component_register(
    SRCS ${APP_SRCS}
    EMBED_TXTFILES ${APP_EMBED_TXTFILES}
    INCLUDE_DIRS
        "."
        "kernel/core/timer"
        "kernel/core/sched"
        "kernel/core/job"
        "kernel/core/ipc"
        "kernel/core/memory"
        "kernel/core/libc"
        "kernel/core/elf"
        "kernel/core/vfs"
        "kernel/vfs"
        "lib"
        "lib/libexec"
    PRIV_REQUIRES
        newlib
        esp_timer
        esp_partition
)

if(CONFIG_MAGNOLIA_ALLOC_ENABLED AND CONFIG_MAGNOLIA_ALLOC_WRAP_LIBC)
    target_link_options(${COMPONENT_LIB} INTERFACE
        "-Wl,--wrap=malloc"
        "-Wl,--wrap=calloc"
        "-Wl,--wrap=realloc"
        "-Wl,--wrap=free"
        "-Wl,--wrap=_malloc_r"
        "-Wl,--wrap=_calloc_r"
        "-Wl,--wrap=_realloc_r"
        "-Wl,--wrap=_free_r"
    )
endif()

# Generate a LittleFS partition image from the repo rootfs/ directory and add
# it to the project's flash (and QEMU) image.
if(CONFIG_MAGNOLIA_VFS_ENABLED AND CONFIG_MAGNOLIA_LITTLEFS_ENABLED)
    set(MAGNOLIA_ROOTFS_DIR "${CMAKE_CURRENT_LIST_DIR}/../rootfs")
    if(EXISTS "${MAGNOLIA_ROOTFS_DIR}")
        set(MAGNOLIA_LFS_PARTITION "${CONFIG_MAGNOLIA_LITTLEFS_PARTITION_LABEL}")
        string(REPLACE "\"" "" MAGNOLIA_LFS_PARTITION "${MAGNOLIA_LFS_PARTITION}")

        partition_table_get_partition_info(MAGNOLIA_LFS_SIZE
                                           "--partition-name ${MAGNOLIA_LFS_PARTITION}"
                                           "size")
        if(MAGNOLIA_LFS_SIZE)
            set(MAGNOLIA_LFS_IMAGE "${CMAKE_BINARY_DIR}/${MAGNOLIA_LFS_PARTITION}.bin")
            set(MAGNOLIA_MKLFS_TOOL "${CMAKE_BINARY_DIR}/magnolia_mklfs${CMAKE_EXECUTABLE_SUFFIX}")

            find_program(MAGNOLIA_HOST_CC NAMES gcc cc)
            if(NOT MAGNOLIA_HOST_CC)
                message(FATAL_ERROR "Host C compiler (gcc/cc) not found; required to build LittleFS image generator.")
            endif()

            add_custom_command(
                OUTPUT ${MAGNOLIA_MKLFS_TOOL}
                COMMAND ${MAGNOLIA_HOST_CC} -O2 -std=c99 -Wall -Wextra
                        -I${CMAKE_CURRENT_LIST_DIR}/kernel/vfs/fs/littlefs
                        -o ${MAGNOLIA_MKLFS_TOOL}
                        ${CMAKE_CURRENT_LIST_DIR}/../tools/magnolia_mklfs.c
                        ${CMAKE_CURRENT_LIST_DIR}/kernel/vfs/fs/littlefs/lfs.c
                        ${CMAKE_CURRENT_LIST_DIR}/kernel/vfs/fs/littlefs/lfs_util.c
                DEPENDS
                    ${CMAKE_CURRENT_LIST_DIR}/../tools/magnolia_mklfs.c
                    ${CMAKE_CURRENT_LIST_DIR}/kernel/vfs/fs/littlefs/lfs.c
                    ${CMAKE_CURRENT_LIST_DIR}/kernel/vfs/fs/littlefs/lfs_util.c
                    ${CMAKE_CURRENT_LIST_DIR}/kernel/vfs/fs/littlefs/lfs.h
                    ${CMAKE_CURRENT_LIST_DIR}/kernel/vfs/fs/littlefs/lfs_util.h
                VERBATIM
            )

            add_custom_target(magnolia_mklfs_tool DEPENDS ${MAGNOLIA_MKLFS_TOOL})

            # Like spiffs_create_partition_image, run this unconditionally since
            # CMake can't easily track directory contents.
            add_custom_target(
                magnolia_rootfs_image
                ALL
                COMMAND ${MAGNOLIA_MKLFS_TOOL}
                        ${MAGNOLIA_ROOTFS_DIR}
                        ${MAGNOLIA_LFS_IMAGE}
                        ${MAGNOLIA_LFS_SIZE}
                        ${CONFIG_MAGNOLIA_LITTLEFS_BLOCK_SIZE}
                        ${CONFIG_MAGNOLIA_LITTLEFS_READ_SIZE}
                        ${CONFIG_MAGNOLIA_LITTLEFS_PROG_SIZE}
                        ${CONFIG_MAGNOLIA_LITTLEFS_CACHE_SIZE}
                        ${CONFIG_MAGNOLIA_LITTLEFS_LOOKAHEAD_SIZE}
                        ${CONFIG_MAGNOLIA_LITTLEFS_BLOCK_CYCLES}
                DEPENDS magnolia_mklfs_tool
                VERBATIM
            )

            set_property(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" APPEND PROPERTY
                ADDITIONAL_CLEAN_FILES
                ${MAGNOLIA_MKLFS_TOOL}
                ${MAGNOLIA_LFS_IMAGE})

            esptool_py_flash_to_partition(flash ${MAGNOLIA_LFS_PARTITION} ${MAGNOLIA_LFS_IMAGE})
            add_dependencies(flash magnolia_rootfs_image)
        endif()
    endif()
endif()
