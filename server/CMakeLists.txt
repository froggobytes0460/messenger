add_library(server_core STATIC src/cli_args.c src/main_socket.c src/poll_mgr.c)
target_include_directories(server_core PUBLIC include/)
target_link_libraries(server_core PRIVATE compiler_flags)

add_executable(server main.c)
target_link_libraries(server PRIVATE server_core compiler_flags)
