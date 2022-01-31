add_library(glkwrap glkwrap.c)

target_include_directories(glkwrap PRIVATE "${CMAKE_SOURCE_DIR}/glkwrap" "${CMAKE_SOURCE_DIR}/winglk/Include")
target_link_directories(glkwrap PRIVATE "${PROJECT_SOURCE_DIR}/winglk")
target_link_libraries(glkwrap Glk)
