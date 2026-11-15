cmake_minimum_required(VERSION 3.18)
project(pocketjni C)

# The emulator core is shared with the Linux build (emu/).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../emu ${CMAKE_CURRENT_BINARY_DIR}/emu)

add_library(pocketjni SHARED jni.c)
target_link_libraries(pocketjni PRIVATE qpcore log)
target_compile_options(pocketjni PRIVATE -Wall -Wextra)
