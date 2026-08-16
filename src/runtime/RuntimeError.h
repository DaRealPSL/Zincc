#pragma once

#include <string>

namespace zinc{

/**
 * somehting that is syntactically and semantically valid but fails during]
 * *execution* (division by zero, index out of bounds, ...). deliberately a
 * separate type from SemanticError/ParseError/LexError so the driver can
 * report "N runtime error(s)" distinctly from compile-time diagnostics.
 */
struct RuntimeError{
    std::string message;
    size_t line, column;
};

/** thrown to unwind the interpreter on a RuntimeError; caught at the top level */
struct RuntimeException{
    RuntimeError error;
};

}//namespace zinc