#pragma once

#include <cstddef>
#include <string>

namespace zinc{

/** every kind of token the lexer can produce, grouped to match zinc.ebnf section I */
enum class TokenType{
    //literals
    IntLiteral,
    FloatLiteral,
    StringLiteral,
    BoolLiteral,
    NullLiteral,
    Identifier,

    //template string pieces (see Lexer.h for how these interleave)
    TemplateStart,      //opening `
    TemplateStringPart, //raw text chunk between { } or delimiters
    TemplateExprStart,  //the { that opens an interpolated expr
    TemplateExprEnd,    //the } that closes an interpolated expr
    TemplateEnd,        //closing `

    //keywords
    Let, Const, Int, Float, Bool, Str, Fn, Ref,
    Class, Extends, Priv, Init, This,
    If, Else, Switch, Case, Default, Break,
    Try, Catch, While, For, In,
    Import, From, Enum, Return, Map,

    //arithmetic / update operators
    Plus, Minus, Star, Slash, Percent, StarStar,
    PlusPlus, MinusMinus,

    //relational operators
    EqualEqual, BangEqual, Less, Greater, LessEqual, GreaterEqual,

    //logical operators
    AmpAmp, PipePipe, Bang,

    //assignment operators
    Equal, PlusEqual, MinusEqual, StarEqual, SlashEqual,

    //lambda
    FatArrow, //=>

    //punctuation
    LBrace, RBrace, LBracket, RBracket, LParen, RParen,
    Colon, Semicolon, Comma, Dot, Question,

    Eof,
    Invalid
};

/** returns a human readable name for diagnostics, e.g. TokenType::Plus -> "Plus" */
const char* tokenTypeName(TokenType type);

/** looks up whether text is a reserved keyword; returns nullptr if it's a plain identifier */
const TokenType* lookupKeyword(const std::string& text);

/** a single lexical token: its kind, exact source text, and 1-based position */
struct Token{
    TokenType type;
    std::string lexeme;
    size_t line;
    size_t column;
};

}//namespace zinc
