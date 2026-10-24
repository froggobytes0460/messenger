add_library(server_core STATIC src/chat.c src/cli_args.c src/log.c
                            src/main_socket.c src/poll_mgr.c src/protocol.c
                            src/auth.c src/uuid.c src/user_store.c)
target_include_directories(server_core PUBLIC include/)
target_link_libraries(server_core PRIVATE
  compiler_flags
  SQLite3::SQLite3
  PkgConfig::LIBSODIUM
  Threads::Threads
)

add_executable(server main.c)
target_link_libraries(server PRIVATE server_core compiler_flags)
