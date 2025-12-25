cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sh)

include(${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__elf_loader/elf_loader.cmake)
project_elf(sh)
