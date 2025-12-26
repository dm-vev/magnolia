idf_component_register(SRCS "dummy.c")

get_filename_component(MAGNOLIA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
include(${MAGNOLIA_ROOT}/cmake/magnolia_zig.cmake)

magnolia_zig_add_obj_to_component(
    COMPONENT_LIB ${COMPONENT_LIB}
    SRC "${CMAKE_CURRENT_LIST_DIR}/zigdemo.zig"
)

