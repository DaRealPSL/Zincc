add_library(zinc_lib STATIC
        lexer/Token.cpp
        lexer/Lexer.cpp
        ast/AstPrinter.cpp
        parser/Parser.cpp
        sema/Type.cpp
        sema/Checker.cpp
        runtime/Value.cpp
        runtime/Interpreter.cpp
        runtime/Builtins.cpp
)

target_include_directories(zinc_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(zincc main.cpp)
target_link_libraries(zincc PRIVATE zinc_lib)