cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rshello)

# Build a Rust static library and link it into the final .app.elf.
set(RSHELLO_RUST_TOOLCHAIN "esp" CACHE STRING "Rust toolchain name to use (as in `cargo +<toolchain>`); set empty to use default")
set(RSHELLO_RUST_TARGET "xtensa-esp32s3-none-elf" CACHE STRING "Rust target triple")
set(RSHELLO_ESPUP_EXPORT "$ENV{HOME}/export-esp.sh" CACHE STRING "Path to espup-generated export script; set empty to skip sourcing it")

set(RSHELLO_RUST_CRATE_DIR "${CMAKE_CURRENT_LIST_DIR}/rust")
set(RSHELLO_RUST_OUT_DIR "${CMAKE_BINARY_DIR}/rust_target")
set(RSHELLO_RUST_LIB "${CMAKE_BINARY_DIR}/librshello.a")

if(RSHELLO_RUST_TOOLCHAIN STREQUAL "")
    set(_RSHELLO_CARGO_TOOLCHAIN "")
else()
    set(_RSHELLO_CARGO_TOOLCHAIN "+${RSHELLO_RUST_TOOLCHAIN}")
endif()

add_custom_command(
    OUTPUT "${RSHELLO_RUST_LIB}"
    COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${RSHELLO_RUST_OUT_DIR}
            bash -lc
            "set -euo pipefail; \
             if [ -n \"${RSHELLO_ESPUP_EXPORT}\" ] && [ -f \"${RSHELLO_ESPUP_EXPORT}\" ]; then . \"${RSHELLO_ESPUP_EXPORT}\" >/dev/null 2>&1; fi; \
             RUSTFLAGS='-C panic=abort' cargo ${_RSHELLO_CARGO_TOOLCHAIN} build \
               -Zbuild-std=core,compiler_builtins -Zbuild-std-features=compiler-builtins-mem \
               --manifest-path '${RSHELLO_RUST_CRATE_DIR}/Cargo.toml' --release --target '${RSHELLO_RUST_TARGET}'"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${RSHELLO_RUST_OUT_DIR}/${RSHELLO_RUST_TARGET}/release/librshello.a"
            "${RSHELLO_RUST_LIB}"
    DEPENDS
            "${RSHELLO_RUST_CRATE_DIR}/Cargo.toml"
            "${RSHELLO_RUST_CRATE_DIR}/.cargo/config.toml"
            "${RSHELLO_RUST_CRATE_DIR}/src/lib.rs"
    VERBATIM
    COMMENT "Build Rust staticlib: ${RSHELLO_RUST_LIB}"
)

add_custom_target(rshello_rustlib DEPENDS "${RSHELLO_RUST_LIB}")

set(ELF_LIBS "${RSHELLO_RUST_LIB}")
set(ELF_DEPENDS rshello_rustlib)

include(${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__elf_loader/elf_loader.cmake)
project_elf(rshello)
