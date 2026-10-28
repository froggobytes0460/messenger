add_library(client_core STATIC)

target_include_directories(client_core PUBLIC include/)

add_executable(client main.c)
target_link_libraries(client PRIVATE client_core compiler_flags)
