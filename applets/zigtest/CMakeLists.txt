cmake_minimum_required(VERSION 3.5)

# Force the applet to build for ESP32-S3 regardless of the user's global IDF target.
set(IDF_TARGET "esp32s3")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(zigtest)

include(${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__elf_loader/elf_loader.cmake)
set(ELF_CFLAGS -Wl,-z,notext -Wl,-z,noexecstack)
project_elf(zigtest)
