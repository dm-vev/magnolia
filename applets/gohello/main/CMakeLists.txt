idf_component_register(SRCS "main.c" "tinygo_symbols.S")

set(GOHELLO_TINYGO_DIR ${CMAKE_CURRENT_LIST_DIR}/../tinygo)
set(GOHELLO_TINYGO_OBJ ${CMAKE_CURRENT_BINARY_DIR}/gohello_tinygo.o)

add_custom_command(
    OUTPUT ${GOHELLO_TINYGO_OBJ}
    COMMAND tinygo build
            -o ${GOHELLO_TINYGO_OBJ}
            -target=esp32-coreboard-v2
            -gc=leaking
            -scheduler=none
            -panic=trap
            -no-debug
            .
    COMMAND xtensa-esp32s3-elf-objcopy
            --set-section-flags .literal=alloc,contents,code,data
            ${GOHELLO_TINYGO_OBJ}
    WORKING_DIRECTORY ${GOHELLO_TINYGO_DIR}
    DEPENDS
        ${GOHELLO_TINYGO_DIR}/go.mod
        ${GOHELLO_TINYGO_DIR}/main.go
    VERBATIM
)

add_custom_target(gohello_tinygo_build DEPENDS ${GOHELLO_TINYGO_OBJ})
add_dependencies(${COMPONENT_LIB} gohello_tinygo_build)
target_sources(${COMPONENT_LIB} PRIVATE ${GOHELLO_TINYGO_OBJ})
