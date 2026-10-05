cmake_minimum_required(VERSION 3.23)
project(Messenger LANGUAGES C)

add_library(compiler_flags INTERFACE)
target_compile_options(compiler_flags INTERFACE -Wall -Wextra -Wpedantic)

add_subdirectory(client/)
add_subdirectory(server/)
