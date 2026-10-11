# One executable per test file; add_messenger_test links it to server_core and
# cmocka, and registers it with CTest.
function(add_messenger_test name)
  add_executable(${name} ${name}.c)
  target_link_libraries(${name} PRIVATE server_core cmocka::cmocka
                                        compiler_flags)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_messenger_test(test_cli_args)
add_messenger_test(test_protocol)
