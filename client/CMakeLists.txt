add_library(client_core STATIC
  src/cli_args.c
  src/compact/getopt.c
  src/compact/getopt.h
)

target_include_directories(client_core
  PUBLIC
    include/
  PRIVATE
    src/
)

add_executable(client main.c)
target_link_libraries(client PRIVATE client_core compiler_flags)
