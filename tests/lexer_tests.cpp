#include "lexer/Lexer.h"
#include "lexer/Token.h"

#include <iostream>
#include <string>
#include <vector>

using namespace zinc;

namespace{

int failures = 0;

/** prints PASS/FAIL for a named check and tracks the failure count */
void check(const std::string& name, bool condition){
    if(condition){
        std::cout << "[PASS] " << name << "\n";
    }else{
        std::cout << "[FAIL] " << name << "\n";
        failures++;
    }
}

std::vector<Token> lex(const std::string& source){
    Lexer lexer(source);
    return lexer.tokenize();
}

std::vector<TokenType> typesOf(const std::vector<Token>& tokens){
    std::vector<TokenType> types;
    for(const Token& t : tokens){
        types.push_back(t.type);
    }
    return types;
}

}//namespace

void testKeywordsAndIdentifiers(){
    auto types = typesOf(lex("let const fn myVar _underscore Class2"));
    std::vector<TokenType> expected = {
        TokenType::Let, TokenType::Const, TokenType::Fn,
        TokenType::Identifier, TokenType::Identifier, TokenType::Identifier,
        TokenType::Eof
    };
    check("keywords and identifiers", types == expected);
}

void testTrueFalseNullAreReserved(){
    auto types = typesOf(lex("true false null"));
    std::vector<TokenType> expected = {
        TokenType::BoolLiteral, TokenType::BoolLiteral, TokenType::NullLiteral, TokenType::Eof
    };
    check("true/false/null lex as literal keywords, not identifiers", types == expected);
}

void testNumbers(){
    auto types = typesOf(lex("42 3.14 0 7.0"));
    std::vector<TokenType> expected = {
        TokenType::IntLiteral, TokenType::FloatLiteral,
        TokenType::IntLiteral, TokenType::FloatLiteral,
        TokenType::Eof
    };
    check("int and float literals", types == expected);
}

void testStringEscapes(){
    auto tokens = lex(R"("hello \"world\"\n")");
    check("string literal token count", tokens.size() == 2);
    check("string literal type", tokens[0].type == TokenType::StringLiteral);
    check("string literal decodes escapes", tokens[0].lexeme == "hello \"world\"\n");
}

void testOperatorsMaximalMunch(){
    auto types = typesOf(lex("+ ++ += - -- -= ** * / /= % == != <= >= < > && || ! = =>"));
    std::vector<TokenType> expected = {
        TokenType::Plus, TokenType::PlusPlus, TokenType::PlusEqual,
        TokenType::Minus, TokenType::MinusMinus, TokenType::MinusEqual,
        TokenType::StarStar, TokenType::Star, TokenType::Slash, TokenType::SlashEqual,
        TokenType::Percent, TokenType::EqualEqual, TokenType::BangEqual,
        TokenType::LessEqual, TokenType::GreaterEqual, TokenType::Less, TokenType::Greater,
        TokenType::AmpAmp, TokenType::PipePipe, TokenType::Bang,
        TokenType::Equal, TokenType::FatArrow,
        TokenType::Eof
    };
    check("operators tokenize with maximal munch", types == expected);
}

void testCommentsAreSkipped(){
    auto types = typesOf(lex("let x = 1 // trailing comment\n/* block\ncomment */ let y = 2"));
    std::vector<TokenType> expected = {
        TokenType::Let, TokenType::Identifier, TokenType::Equal, TokenType::IntLiteral,
        TokenType::Let, TokenType::Identifier, TokenType::Equal, TokenType::IntLiteral,
        TokenType::Eof
    };
    check("line and block comments are skipped", types == expected);
}

void testTemplateStringSimple(){
    auto tokens = lex(R"(`hi {name}!`)");
    auto types = typesOf(tokens);
    std::vector<TokenType> expected = {
        TokenType::TemplateStart, TokenType::TemplateStringPart, TokenType::TemplateExprStart,
        TokenType::Identifier, TokenType::TemplateExprEnd, TokenType::TemplateStringPart,
        TokenType::TemplateEnd, TokenType::Eof
    };
    check("template string with interpolation", types == expected);
    check("template text before expr", tokens[1].lexeme == "hi ");
    check("template text after expr", tokens[5].lexeme == "!");
}

void testTemplateStringWithNestedMapLiteral(){
    //the { and } belonging to the map literal inside the interpolation must
    //NOT be mistaken for the template's own TemplateExprEnd
    auto types = typesOf(lex(R"(`val {f({"a": 1})}`)"));
    std::vector<TokenType> expected = {
        TokenType::TemplateStart, TokenType::TemplateStringPart, TokenType::TemplateExprStart,
        TokenType::Identifier, TokenType::LParen, TokenType::LBrace, TokenType::StringLiteral,
        TokenType::Colon, TokenType::IntLiteral, TokenType::RBrace, TokenType::RParen,
        TokenType::TemplateExprEnd, TokenType::TemplateEnd,
        TokenType::Eof
    };
    check("template interpolation with nested {} doesn't close early", types == expected);
}

void testNestedTemplateInsideInterpolation(){
    //a template can contain an expr that itself contains another template
    auto types = typesOf(lex(R"(`outer {`inner {x}`}`)"));
    std::vector<TokenType> expected = {
        TokenType::TemplateStart, TokenType::TemplateStringPart, TokenType::TemplateExprStart,
        TokenType::TemplateStart, TokenType::TemplateStringPart, TokenType::TemplateExprStart,
        TokenType::Identifier, TokenType::TemplateExprEnd, TokenType::TemplateEnd,
        TokenType::TemplateExprEnd, TokenType::TemplateEnd,
        TokenType::Eof
    };
    check("nested template inside an interpolated expr", types == expected);
}

void testUnterminatedStringReportsError(){
    Lexer lexer("\"unterminated");
    lexer.tokenize();
    check("unterminated string produces a lex error", !lexer.errors().empty());
}

void testUnterminatedTemplateReportsError(){
    Lexer lexer("`unterminated");
    lexer.tokenize();
    check("unterminated template produces a lex error", !lexer.errors().empty());
}

int main(){
    testKeywordsAndIdentifiers();
    testTrueFalseNullAreReserved();
    testNumbers();
    testStringEscapes();
    testOperatorsMaximalMunch();
    testCommentsAreSkipped();
    testTemplateStringSimple();
    testTemplateStringWithNestedMapLiteral();
    testNestedTemplateInsideInterpolation();
    testUnterminatedStringReportsError();
    testUnterminatedTemplateReportsError();

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
