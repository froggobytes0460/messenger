cmake_minimum_required(VERSION 3.23)
project(Messenger LANGUAGES C)

add_library(compiler_flags INTERFACE)
target_compile_options(compiler_flags INTERFACE -Wall -Wextra -Wpedantic)

add_subdirectory(client/)
add_subdirectory(server/)

find_package(Doxygen OPTIONAL_COMPONENTS dot)
if(DOXYGEN_FOUND)
  set(DOXYGEN_PROJECT_NAME "Messenger")
  set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/docs/)
  set(DOXYGEN_OPTIMIZE_OUTPUT_FOR_C YES)
  set(DOXYGEN_EXTRACT_ALL NO)
  set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)
  set(DOXYGEN_USE_MDFILE_AS_MAINPAGE ${CMAKE_SOURCE_DIR}/README.md)
  set(DOXYGEN_GENERATE_LATEX NO)
  doxygen_add_docs(docs 
    server/include server/src server/main.c client/ README.md
    COMMENT "Generating API documentation")
endif()
