cmake_minimum_required(VERSION 3.5)

# Force the applet to build for ESP32-S3 regardless of the user's global IDF target.
set(IDF_TARGET "esp32s3")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rselftest)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/magnolia_rust_applet.cmake)
magnolia_rust_project_elf(rselftest CRATE_DIR "${CMAKE_CURRENT_LIST_DIR}/rust")

