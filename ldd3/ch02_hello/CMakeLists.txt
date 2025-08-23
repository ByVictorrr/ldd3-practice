cmake_minimum_required(VERSION 3.18)
project(ch2)
file(GLOB CH02_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/*.[ch])
list(FILTER CH02_SRC EXCLUDE REGEX "\\.mod\\.c$")
add_library(ch02_hello_idx STATIC EXCLUDE_FROM_ALL ${CH02_SRC})
target_link_libraries(ch02_hello_idx PRIVATE kbuild_flags)

