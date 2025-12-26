# Magnolia Zig SDK helpers for building ELF applets.
#
# This wraps the existing `zig build-obj` flow used by `applets/zighello`:
# - Ensures an Xtensa-capable Zig toolchain is installed (tools/zig_xtensa_toolchain.py)
# - Compiles a Zig root file into an object suitable for `project_elf()` applet linking
# - Exposes the Zig module `magnolia` (sdk/zig/magnolia.zig) to the root module
# - Applies an objcopy fixup for `.literal` so relocations remain writable in ET_DYN
#
# Usage (inside an applet component's main/CMakeLists.txt):
#
#   get_filename_component(MAGNOLIA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
#   include(${MAGNOLIA_ROOT}/cmake/magnolia_zig.cmake)
#   idf_component_register(SRCS "dummy.c")
#   magnolia_zig_add_obj_to_component(
#       COMPONENT_LIB ${COMPONENT_LIB}
#       SRC ${CMAKE_CURRENT_LIST_DIR}/my_applet.zig
#   )

include_guard(GLOBAL)

if(NOT DEFINED MAGNOLIA_ZIG_PIC)
    # NOTE: Xtensa PIC in Zig is still limited; non-PIC objects + text relocs
    # are currently the most reliable way to build featureful applets.
    set(MAGNOLIA_ZIG_PIC OFF)
endif()

# Capture repo root at include-time; `CMAKE_CURRENT_LIST_DIR` is dynamic and
# would otherwise point at the caller when the function executes.
get_filename_component(_MAGNOLIA_ZIG_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

function(magnolia_zig_add_obj_to_component)
    set(options "")
    set(oneValueArgs COMPONENT_LIB SRC OUTPUT_NAME TARGET MCPU OPTIMIZE)
    set(multiValueArgs EXTRA_DEPS ZIG_FLAGS)
    cmake_parse_arguments(MZIG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT MZIG_COMPONENT_LIB)
        message(FATAL_ERROR "magnolia_zig_add_obj_to_component: COMPONENT_LIB is required")
    endif()
    if(NOT MZIG_SRC)
        message(FATAL_ERROR "magnolia_zig_add_obj_to_component: SRC is required")
    endif()

    set(MAGNOLIA_ROOT "${_MAGNOLIA_ZIG_REPO_ROOT}")

    if(NOT DEFINED MAGNOLIA_ZIG_TOOLCHAIN_DIR)
        set(MAGNOLIA_ZIG_TOOLCHAIN_DIR "${MAGNOLIA_ROOT}/build/toolchains/zig-xtensa")
    endif()
    set(MAGNOLIA_ZIG_BIN "${MAGNOLIA_ZIG_TOOLCHAIN_DIR}/zig")

    set(MAGNOLIA_ZIG_SDK_MODULE "${MAGNOLIA_ROOT}/sdk/zig/magnolia.zig")
    set(MAGNOLIA_ZIG_SHIM_C "${MAGNOLIA_ROOT}/sdk/zig/magnolia_shim.c")

    if(NOT EXISTS "${MAGNOLIA_ZIG_SDK_MODULE}")
        message(FATAL_ERROR "Missing Zig SDK module: ${MAGNOLIA_ZIG_SDK_MODULE}")
    endif()
    if(NOT EXISTS "${MAGNOLIA_ZIG_SHIM_C}")
        message(FATAL_ERROR "Missing Zig SDK shim: ${MAGNOLIA_ZIG_SHIM_C}")
    endif()

    if(NOT MZIG_TARGET)
        set(MZIG_TARGET "xtensa-freestanding-none")
    endif()

    if(NOT MZIG_MCPU)
        if(DEFINED IDF_TARGET AND NOT IDF_TARGET STREQUAL "")
            set(MZIG_MCPU "${IDF_TARGET}")
        else()
            # Repo default is ESP32-S3.
            set(MZIG_MCPU "esp32s3")
        endif()
    endif()

    if(NOT MZIG_OPTIMIZE)
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(MZIG_OPTIMIZE "-ODebug")
        else()
            set(MZIG_OPTIMIZE "-OReleaseSmall")
        endif()
    endif()

    if(MZIG_OUTPUT_NAME)
        set(_mzig_name "${MZIG_OUTPUT_NAME}")
    else()
        get_filename_component(_mzig_name "${MZIG_SRC}" NAME_WE)
    endif()

    set(_mzig_obj "${CMAKE_CURRENT_BINARY_DIR}/${_mzig_name}.zig.o")

    # Add the shim C file once per component (provides magnolia_errno()).
    get_target_property(_mzig_shim_added "${MZIG_COMPONENT_LIB}" MAGNOLIA_ZIG_SHIM_ADDED)
    if(NOT _mzig_shim_added)
        target_sources(${MZIG_COMPONENT_LIB} PRIVATE "${MAGNOLIA_ZIG_SHIM_C}")
        set_property(TARGET ${MZIG_COMPONENT_LIB} PROPERTY MAGNOLIA_ZIG_SHIM_ADDED TRUE)
    endif()

    # Track all SDK Zig sources as dependencies.
    file(GLOB_RECURSE _mzig_sdk_deps CONFIGURE_DEPENDS
        "${MAGNOLIA_ROOT}/sdk/zig/magnolia.zig"
        "${MAGNOLIA_ROOT}/sdk/zig/magnolia/*.zig"
    )

    add_custom_command(
        OUTPUT "${_mzig_obj}"
        COMMAND python3 "${MAGNOLIA_ROOT}/tools/zig_xtensa_toolchain.py" --install-dir "${MAGNOLIA_ZIG_TOOLCHAIN_DIR}"
        COMMAND "${MAGNOLIA_ZIG_BIN}" build-obj
                -target ${MZIG_TARGET}
                -mcpu ${MZIG_MCPU}
                ${_mzig_pic_flag}
                "${MZIG_OPTIMIZE}"
                ${MZIG_ZIG_FLAGS}
                --dep magnolia
                -Mroot=${MZIG_SRC}
                -Mmagnolia=${MAGNOLIA_ZIG_SDK_MODULE}
                -femit-bin=${_mzig_obj}
        # Xtensa PIC from LLVM emits a dedicated `.literal` section with relocations.
        # Mark it writable so the ELF applet link (ET_DYN) can keep dynamic relocs.
        COMMAND "${CMAKE_OBJCOPY}" --set-section-flags .literal=alloc,load,contents,data "${_mzig_obj}"
        DEPENDS
                "${MZIG_SRC}"
                "${MAGNOLIA_ROOT}/tools/zig_xtensa_toolchain.py"
                ${_mzig_sdk_deps}
                ${MZIG_EXTRA_DEPS}
        VERBATIM
        COMMENT "Zig build-obj: ${_mzig_name} -> ${_mzig_obj}"
    )

    add_custom_target("${_mzig_name}_zig_obj" DEPENDS "${_mzig_obj}")
    add_dependencies(${MZIG_COMPONENT_LIB} "${_mzig_name}_zig_obj")
    target_sources(${MZIG_COMPONENT_LIB} PRIVATE "${_mzig_obj}")
endfunction()
    set(_mzig_pic_flag "")
    if(MAGNOLIA_ZIG_PIC)
        set(_mzig_pic_flag "-fPIC")
    endif()
