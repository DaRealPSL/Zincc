#pragma once

#include <string>
#include <vector>

#include "lexer/Token.h"

namespace zinc{

/** a lexical error with enough context to point the user at the problem */
struct LexError{
    std::string message;
    size_t line;
    size_t column;
};

/**
 * turns zinc source text into a flat token stream.
 *
 * template strings are the one construct that isn't context-free at the
 * character level: `hi {name}!` must treat the { as an expression boundary,
 * but an expression inside it can itself contain braces (a map literal) or
 * even another nested template. the lexer tracks this with a small mode
 * stack: entering a template pushes InTemplate, hitting { pushes
 * InTemplateExpr, and only a } seen while that frame's own brace depth is
 * zero closes the interpolation - any deeper { and } just pass through as
 * ordinary LBrace/RBrace tokens.
 */
class Lexer{
public:
    explicit Lexer(std::string source);

    /** tokenizes the whole source; check errors() afterward for problems found along the way */
    std::vector<Token> tokenize();

    const std::vector<LexError>& errors() const{ return errors_; }

private:
    enum class Mode{
        Normal,
        InTemplate,
        InTemplateExpr
    };

    struct ModeFrame{
        Mode mode;
        int braceDepth = 0; //only meaningful for InTemplateExpr frames
    };

    std::string source_;
    size_t pos_ = 0;
    size_t line_ = 1;
    size_t column_ = 1;
    std::vector<ModeFrame> modeStack_;
    std::vector<LexError> errors_;

    bool isAtEnd() const;
    char peek(size_t offset = 0) const;
    char advance();
    bool match(char expected);

    void skipWhitespaceAndComments();

    Token makeToken(TokenType type, const std::string& lexeme, size_t line, size_t column);
    void reportError(const std::string& message, size_t line, size_t column);

    Token nextNormalToken();
    Token nextTemplateStringPart();

    Token lexIdentifierOrKeyword();
    Token lexNumber();
    Token lexStringLiteral();
    std::string lexEscapeSequence(char terminator);
};

}//namespace zinc
