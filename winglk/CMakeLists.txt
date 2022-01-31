cmake_minimum_required(VERSION 3.3)

project("winglk-terps")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/../cmake/Modules/")
include(Compilers)

if(POLICY CMP0069)
    cmake_policy(SET CMP0069 NEW)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT HAS_LTO)
endif()

add_subdirectory(glkwrap)
add_subdirectory(../terps terps)
