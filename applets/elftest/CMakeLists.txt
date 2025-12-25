cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(elftest)

# Use the ELF loader helper to produce an ELF application artifact.
include(elf_loader)
project_elf(elftest)
