cmake_minimum_required(VERSION 3.5)

# MagnoliaOS: monolithic ESP-IDF app that leans on FreeRTOS + VFS instead of
# the original xv6 / ELF loader example.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(magnolia)
