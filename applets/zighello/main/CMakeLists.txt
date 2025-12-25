idf_component_register(SRCS "dummy.c")

get_filename_component(MAGNOLIA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(ZIG_XTENSA_DIR "${MAGNOLIA_ROOT}/build/toolchains/zig-xtensa")
set(ZIG_XTENSA_BIN "${ZIG_XTENSA_DIR}/zig")

set(ZIGHELLO_ZIG_SRC "${CMAKE_CURRENT_LIST_DIR}/zighello.zig")
set(ZIGHELLO_ZIG_OBJ "${CMAKE_CURRENT_BINARY_DIR}/zighello.zig.o")

add_custom_command(
    OUTPUT "${ZIGHELLO_ZIG_OBJ}"
    COMMAND python3 ${MAGNOLIA_ROOT}/tools/zig_xtensa_toolchain.py --install-dir ${ZIG_XTENSA_DIR}
    COMMAND ${ZIG_XTENSA_BIN} build-obj
            -target xtensa-freestanding-none
            -mcpu=esp32s3
            -fPIC
            -OReleaseSmall
            -femit-bin=${ZIGHELLO_ZIG_OBJ}
            ${ZIGHELLO_ZIG_SRC}
    # Xtensa PIC from LLVM emits a dedicated `.literal` section with relocations.
    # Mark it writable so the ELF applet link (ET_DYN) can keep dynamic relocs.
    COMMAND ${CMAKE_OBJCOPY} --set-section-flags .literal=alloc,load,contents,data ${ZIGHELLO_ZIG_OBJ}
    DEPENDS "${ZIGHELLO_ZIG_SRC}" "${MAGNOLIA_ROOT}/tools/zig_xtensa_toolchain.py"
    VERBATIM
)

add_custom_target(zighello_zig_obj DEPENDS "${ZIGHELLO_ZIG_OBJ}")
add_dependencies(${COMPONENT_LIB} zighello_zig_obj)
target_sources(${COMPONENT_LIB} PRIVATE "${ZIGHELLO_ZIG_OBJ}")

