add_executable(lexer_tests lexer_tests.cpp)
target_link_libraries(lexer_tests PRIVATE zinc_lib)

add_executable(parser_tests parser_tests.cpp)
target_link_libraries(parser_tests PRIVATE zinc_lib)

add_executable(checker_tests checker_tests.cpp)
target_link_libraries(checker_tests PRIVATE zinc_lib)
