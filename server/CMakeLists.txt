add_compile_definitions(_POSIX_C_SOURCE=200809L)

add_executable(server main.c)
target_include_directories(server PRIVATE include/)

target_link_libraries(server PRIVATE compiler_flags)
