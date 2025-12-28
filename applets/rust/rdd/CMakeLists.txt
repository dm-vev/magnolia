cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(dd)

include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/magnolia_rust_applet.cmake)
magnolia_rust_project_elf(dd CRATE_DIR "${CMAKE_CURRENT_LIST_DIR}/rust")
