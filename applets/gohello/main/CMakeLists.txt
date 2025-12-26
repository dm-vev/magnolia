include(${CMAKE_CURRENT_LIST_DIR}/../../../sdk/tinygo/cmake/MagnoliaTinyGo.cmake)

idf_component_register(SRCS "main.c" ${MAGNOLIA_TINYGO_RUNTIME_SYMBOLS})

magnolia_tinygo_add_object(
    TARGET ${COMPONENT_LIB}
    NAME   gohello
    GO_DIR ${CMAKE_CURRENT_LIST_DIR}/../tinygo
)
