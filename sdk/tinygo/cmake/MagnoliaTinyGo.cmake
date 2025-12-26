# Magnolia TinyGo helpers for ESP-IDF applet projects.
#
# Typical usage (inside applet `main/CMakeLists.txt`):
#
#   include(${CMAKE_CURRENT_LIST_DIR}/../../../sdk/tinygo/cmake/MagnoliaTinyGo.cmake)
#   idf_component_register(SRCS ${MAGNOLIA_TINYGO_RUNTIME_SYMBOLS})
#   magnolia_tinygo_add_object(
#       TARGET  ${COMPONENT_LIB}
#       NAME    myapplet
#       GO_DIR  ${CMAKE_CURRENT_LIST_DIR}/../tinygo
#   )
#
# And in applet root `CMakeLists.txt` (before `project_elf()`):
#
#   include(${CMAKE_CURRENT_LIST_DIR}/../../sdk/tinygo/cmake/MagnoliaTinyGo.cmake)
#   magnolia_tinygo_enable_text_relocs()

set(MAGNOLIA_TINYGO_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/.." CACHE INTERNAL "Magnolia TinyGo SDK dir")
get_filename_component(MAGNOLIA_TINYGO_SDK_DIR "${MAGNOLIA_TINYGO_SDK_DIR}" ABSOLUTE)

set(MAGNOLIA_TINYGO_RUNTIME_SYMBOLS "${MAGNOLIA_TINYGO_SDK_DIR}/runtime/tinygo_symbols.S" CACHE INTERNAL "TinyGo runtime symbols for Magnolia")

set(MAGNOLIA_TINYGO_TARGET "esp32-coreboard-v2" CACHE STRING "TinyGo -target for Magnolia applets")
set(MAGNOLIA_TINYGO_HEAP_SIZE "4096" CACHE STRING "TinyGo heap size (bytes) for Magnolia applets")

set(MAGNOLIA_TINYGO_DEFAULT_FLAGS
    -gc=leaking
    -scheduler=none
    -panic=trap
    -no-debug
    CACHE STRING "Default TinyGo flags for Magnolia applets"
)

function(_magnolia_find_objcopy out_var)
    if(DEFINED CMAKE_OBJCOPY AND NOT "${CMAKE_OBJCOPY}" STREQUAL "")
        set(${out_var} "${CMAKE_OBJCOPY}" PARENT_SCOPE)
        return()
    endif()

    find_program(_MAGNOLIA_LLVM_OBJCOPY
        NAMES llvm-objcopy llvm-objcopy-19 llvm-objcopy-18 llvm-objcopy-17 llvm-objcopy-16
    )
    if(_MAGNOLIA_LLVM_OBJCOPY)
        set(${out_var} "${_MAGNOLIA_LLVM_OBJCOPY}" PARENT_SCOPE)
        return()
    endif()

    find_program(_MAGNOLIA_OBJCOPY NAMES objcopy)
    if(_MAGNOLIA_OBJCOPY)
        set(${out_var} "${_MAGNOLIA_OBJCOPY}" PARENT_SCOPE)
        return()
    endif()

    set(${out_var} "" PARENT_SCOPE)
endfunction()

function(magnolia_tinygo_enable_text_relocs)
    # TinyGo Xtensa objects currently rely on relocations that touch `.literal`.
    # Linking the final applet with `-z,notext` makes this reliable.
    set(extra_flags "-Wl,-z,noexecstack")
    if(DEFINED CONFIG_IDF_TARGET_ARCH_XTENSA AND CONFIG_IDF_TARGET_ARCH_XTENSA)
        list(PREPEND extra_flags "-Wl,-z,notext")
    endif()

    if(DEFINED ELF_CFLAGS)
        set(ELF_CFLAGS ${ELF_CFLAGS} ${extra_flags} PARENT_SCOPE)
    else()
        set(ELF_CFLAGS ${extra_flags} PARENT_SCOPE)
    endif()
endfunction()

function(magnolia_tinygo_add_runtime_symbols)
    set(oneValueArgs TARGET HEAP_SIZE)
    cmake_parse_arguments(MTGO "" "${oneValueArgs}" "" ${ARGN})

    if(NOT MTGO_TARGET)
        message(FATAL_ERROR "magnolia_tinygo_add_runtime_symbols: TARGET is required")
    endif()

    target_sources(${MTGO_TARGET} PRIVATE "${MAGNOLIA_TINYGO_RUNTIME_SYMBOLS}")

    if(MTGO_HEAP_SIZE)
        target_compile_definitions(${MTGO_TARGET} PRIVATE MAGNOLIA_TINYGO_HEAP_SIZE=${MTGO_HEAP_SIZE})
    elseif(DEFINED MAGNOLIA_TINYGO_HEAP_SIZE AND NOT "${MAGNOLIA_TINYGO_HEAP_SIZE}" STREQUAL "")
        target_compile_definitions(${MTGO_TARGET} PRIVATE MAGNOLIA_TINYGO_HEAP_SIZE=${MAGNOLIA_TINYGO_HEAP_SIZE})
    endif()
endfunction()

function(magnolia_tinygo_add_object)
    set(oneValueArgs TARGET NAME GO_DIR OUT_OBJ TINYGO_TARGET)
    set(multiValueArgs TINYGO_FLAGS)
    cmake_parse_arguments(MTGO "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT MTGO_TARGET)
        message(FATAL_ERROR "magnolia_tinygo_add_object: TARGET is required")
    endif()
    if(NOT MTGO_GO_DIR)
        message(FATAL_ERROR "magnolia_tinygo_add_object: GO_DIR is required")
    endif()
    if(NOT MTGO_NAME)
        message(FATAL_ERROR "magnolia_tinygo_add_object: NAME is required")
    endif()

    get_filename_component(go_dir "${MTGO_GO_DIR}" ABSOLUTE)
    if(NOT EXISTS "${go_dir}/go.mod")
        message(FATAL_ERROR "magnolia_tinygo_add_object: missing go.mod in ${go_dir}")
    endif()

    if(MTGO_OUT_OBJ)
        set(out_obj "${MTGO_OUT_OBJ}")
    else()
        set(out_obj "${CMAKE_CURRENT_BINARY_DIR}/${MTGO_NAME}_tinygo.o")
    endif()

    if(MTGO_TINYGO_TARGET)
        set(tg_target "${MTGO_TINYGO_TARGET}")
    else()
        set(tg_target "${MAGNOLIA_TINYGO_TARGET}")
    endif()

    set(tg_flags ${MAGNOLIA_TINYGO_DEFAULT_FLAGS} ${MTGO_TINYGO_FLAGS})

    file(GLOB_RECURSE go_sources CONFIGURE_DEPENDS
        "${go_dir}/*.go"
        "${go_dir}/*.s"
        "${go_dir}/*.S"
    )
    set(go_deps ${go_sources} "${go_dir}/go.mod")
    if(EXISTS "${go_dir}/go.sum")
        list(APPEND go_deps "${go_dir}/go.sum")
    endif()

    _magnolia_find_objcopy(objcopy_bin)

    set(objcopy_cmd "")
    if(DEFINED CONFIG_IDF_TARGET_ARCH_XTENSA AND CONFIG_IDF_TARGET_ARCH_XTENSA)
        if(objcopy_bin)
            set(objcopy_cmd
                COMMAND ${objcopy_bin}
                        --set-section-flags .literal=alloc,contents,code,data
                        ${out_obj}
            )
        else()
            message(WARNING "TinyGo Xtensa post-process skipped (objcopy not found); relocations may fail at runtime")
        endif()
    endif()

    add_custom_command(
        OUTPUT ${out_obj}
        COMMAND tinygo build
                -o ${out_obj}
                -target=${tg_target}
                ${tg_flags}
                .
        ${objcopy_cmd}
        WORKING_DIRECTORY ${go_dir}
        DEPENDS ${go_deps}
        VERBATIM
    )

    add_custom_target(${MTGO_NAME}_tinygo_build DEPENDS ${out_obj})
    add_dependencies(${MTGO_TARGET} ${MTGO_NAME}_tinygo_build)
    target_sources(${MTGO_TARGET} PRIVATE ${out_obj})
endfunction()
