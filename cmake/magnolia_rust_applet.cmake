# Magnolia Rust applet helpers (build Rust -> staticlib and link into .app.elf).
#
# Usage (from an applet directory CMakeLists.txt):
#
#   include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/magnolia_rust_applet.cmake)
#   magnolia_rust_project_elf(my_applet CRATE_DIR "${CMAKE_CURRENT_LIST_DIR}/rust")
#
# Cache variables (override per-app if needed):
# - MAGNOLIA_RUST_TOOLCHAIN: Rust toolchain name to use (cargo +<toolchain>); empty to use default
# - MAGNOLIA_RUST_TARGET: Rust target triple (e.g. xtensa-esp32s3-none-elf)
# - MAGNOLIA_ESPUP_EXPORT: Path to espup-generated export script; empty to skip sourcing it

set(MAGNOLIA_RUST_TOOLCHAIN "esp" CACHE STRING "Rust toolchain name to use (as in `cargo +<toolchain>`); set empty to use default")
set(MAGNOLIA_RUST_TARGET "xtensa-esp32s3-none-elf" CACHE STRING "Rust target triple")
set(MAGNOLIA_ESPUP_EXPORT "$ENV{HOME}/export-esp.sh" CACHE STRING "Path to espup-generated export script; set empty to skip sourcing it")

get_filename_component(_MAGNOLIA_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

macro(magnolia_rust_project_elf project_name)
    cmake_parse_arguments(MRPE "" "CRATE_DIR;CRATE_NAME;RUST_TARGET;RUST_TOOLCHAIN;ESPUP_EXPORT;BUILD_STD;BUILD_STD_FEATURES;RUSTFLAGS" "FEATURES" ${ARGN})

    if(NOT MRPE_CRATE_DIR)
        message(FATAL_ERROR "magnolia_rust_project_elf(${project_name}): CRATE_DIR is required")
    endif()

    get_filename_component(_MRPE_CRATE_DIR "${MRPE_CRATE_DIR}" ABSOLUTE)

    if(MRPE_CRATE_NAME)
        set(_MRPE_CRATE_NAME "${MRPE_CRATE_NAME}")
    else()
        set(_MRPE_CRATE_NAME "${project_name}")
    endif()
    string(REPLACE "-" "_" _MRPE_CRATE_LIB_NAME "${_MRPE_CRATE_NAME}")

    if(MRPE_RUST_TARGET)
        set(_MRPE_RUST_TARGET "${MRPE_RUST_TARGET}")
    else()
        set(_MRPE_RUST_TARGET "${MAGNOLIA_RUST_TARGET}")
    endif()

    if(MRPE_RUST_TOOLCHAIN)
        set(_MRPE_RUST_TOOLCHAIN "${MRPE_RUST_TOOLCHAIN}")
    else()
        set(_MRPE_RUST_TOOLCHAIN "${MAGNOLIA_RUST_TOOLCHAIN}")
    endif()
    if(_MRPE_RUST_TOOLCHAIN STREQUAL "")
        set(_MRPE_CARGO_TOOLCHAIN "")
    else()
        set(_MRPE_CARGO_TOOLCHAIN "+${_MRPE_RUST_TOOLCHAIN}")
    endif()

    if(MRPE_ESPUP_EXPORT)
        set(_MRPE_ESPUP_EXPORT "${MRPE_ESPUP_EXPORT}")
    else()
        set(_MRPE_ESPUP_EXPORT "${MAGNOLIA_ESPUP_EXPORT}")
    endif()

    if(MRPE_BUILD_STD)
        set(_MRPE_BUILD_STD "${MRPE_BUILD_STD}")
    else()
        set(_MRPE_BUILD_STD "core,alloc,compiler_builtins")
    endif()
    if(MRPE_BUILD_STD_FEATURES)
        set(_MRPE_BUILD_STD_FEATURES "${MRPE_BUILD_STD_FEATURES}")
    else()
        set(_MRPE_BUILD_STD_FEATURES "compiler-builtins-mem")
    endif()

    if(MRPE_RUSTFLAGS)
        set(_MRPE_RUSTFLAGS "${MRPE_RUSTFLAGS}")
    else()
        # Xtensa Rust toolchains may not support PIC codegen; rely on dynamic
        # relocations in the final ET_DYN instead (kernel supports RELATIVE/GLOB_DAT/JMP_SLOT).
        set(_MRPE_RUSTFLAGS "-C panic=abort")
    endif()

    set(_MRPE_FEATURE_FLAGS "")
    if(MRPE_FEATURES)
        string(JOIN "," _MRPE_FEATURES_JOINED ${MRPE_FEATURES})
        set(_MRPE_FEATURE_FLAGS "--features ${_MRPE_FEATURES_JOINED}")
    endif()

    set(_MRPE_RUST_OUT_DIR "${CMAKE_BINARY_DIR}/rust_target")
    set(_MRPE_RUST_LIB "${CMAKE_BINARY_DIR}/lib${project_name}.a")
    set(_MRPE_RUST_CARGO_LIB "${_MRPE_RUST_OUT_DIR}/${_MRPE_RUST_TARGET}/release/lib${_MRPE_CRATE_LIB_NAME}.a")

    if(EXISTS "${_MRPE_CRATE_DIR}/Cargo.lock")
        set(_MRPE_LOCKED_FLAG "--locked")
    else()
        set(_MRPE_LOCKED_FLAG "")
    endif()

    file(GLOB_RECURSE _MRPE_RUST_SOURCES CONFIGURE_DEPENDS "${_MRPE_CRATE_DIR}/src/*.rs")
    file(GLOB_RECURSE _MRPE_SDK_SOURCES CONFIGURE_DEPENDS "${_MAGNOLIA_ROOT}/sdk/rust/*/src/*.rs" "${_MAGNOLIA_ROOT}/sdk/rust/*/Cargo.toml")

    add_custom_command(
        OUTPUT "${_MRPE_RUST_LIB}"
        COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${_MRPE_RUST_OUT_DIR}
                bash -lc
                "set -euo pipefail; \
                 if [ -n \"${_MRPE_ESPUP_EXPORT}\" ] && [ -f \"${_MRPE_ESPUP_EXPORT}\" ]; then . \"${_MRPE_ESPUP_EXPORT}\" >/dev/null 2>&1; fi; \
                 RUSTFLAGS='${_MRPE_RUSTFLAGS}' cargo ${_MRPE_CARGO_TOOLCHAIN} build ${_MRPE_FEATURE_FLAGS} ${_MRPE_LOCKED_FLAG} \
                   -Zbuild-std=${_MRPE_BUILD_STD} -Zbuild-std-features=${_MRPE_BUILD_STD_FEATURES} \
                   --manifest-path '${_MRPE_CRATE_DIR}/Cargo.toml' --release --target '${_MRPE_RUST_TARGET}'"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_MRPE_RUST_CARGO_LIB}"
                "${_MRPE_RUST_LIB}"
        COMMAND bash -lc
                "set -euo pipefail; \
                 tmp='${CMAKE_BINARY_DIR}/rust_fixup_${project_name}'; \
                 rm -rf \"$tmp\"; mkdir -p \"$tmp\"; \
                 cd \"$tmp\"; \
                 '${CMAKE_AR}' x '${_MRPE_RUST_LIB}'; \
                 for obj in *.o; do \
                   for sec in $(LC_ALL=C readelf -S -W \"$obj\" | sed -n 's/^[[:space:]]*\\[[[:space:]]*[0-9]\\+\\][[:space:]]\\+\\([^ ]\\+\\).*/\\1/p'); do \
                     case \"$sec\" in \
                       .literal*|.rodata*) '${CMAKE_OBJCOPY}' --set-section-flags \"$sec\"=alloc,load,contents,data \"$obj\" ;; \
                     esac; \
                   done; \
                 done; \
                 '${CMAKE_AR}' rcs '${_MRPE_RUST_LIB}' *.o"
        DEPENDS
                "${_MRPE_CRATE_DIR}/Cargo.toml"
                "${_MRPE_CRATE_DIR}/Cargo.lock"
                ${_MRPE_RUST_SOURCES}
                ${_MRPE_SDK_SOURCES}
        VERBATIM
        COMMENT "Build Rust staticlib: ${_MRPE_RUST_LIB}"
    )

    add_custom_target(${project_name}_rustlib DEPENDS "${_MRPE_RUST_LIB}")

    if(ELF_LIBS)
        list(APPEND ELF_LIBS "${_MRPE_RUST_LIB}")
    else()
        set(ELF_LIBS "${_MRPE_RUST_LIB}")
    endif()

    if(ELF_DEPENDS)
        list(APPEND ELF_DEPENDS ${project_name}_rustlib)
    else()
        set(ELF_DEPENDS ${project_name}_rustlib)
    endif()

    # Rust Xtensa backends may emit literal pools/rodata with dynamic relocations
    # that the Xtensa linker rejects by default when producing ET_DYN.
    # Allow text relocs for Rust applets (Magnolia loader applies relocations in RAM).
    if(ELF_CFLAGS)
        list(APPEND ELF_CFLAGS -Wl,-z,notext -Wl,-z,noexecstack)
    else()
        set(ELF_CFLAGS -Wl,-z,notext -Wl,-z,noexecstack)
    endif()

    include(${_MAGNOLIA_ROOT}/managed_components/espressif__elf_loader/elf_loader.cmake)
    project_elf(${project_name})
endmacro()
