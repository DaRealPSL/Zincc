#pragma once

#include <string>
#include <vector>

#include "ast/Ast.h"
#include "lexer/Token.h"

namespace zinc{

struct ParseError{
    std::string message;
    size_t line, column;
};

/**
 * recursive-descent parser mirroring the precedence chain in zinc.ebnf
 * section II.7 exactly (Assignment -> Logical -> Relational -> Additive ->
 * Multiplicative -> Exponent -> Unary -> Postfix -> Primary).
 *
 * on a syntax error, the parser records it and calls synchronize() to skip
 * forward to a likely statement boundary (past a ';', or up to a token that
 * starts a new statement) rather than aborting, so a single run can report
 * more than one error.
 */
class Parser{
public:
    explicit Parser(std::vector<Token> tokens);

    Program parseProgram();

    const std::vector<ParseError>& errors() const{ return errors_; }

private:
    //thrown internally to unwind out of a broken statement/expression and
    //into parseProgram()'s recovery loop; never escapes the class
    struct ParseException{};

    std::vector<Token> tokens_;
    size_t pos_ = 0;
    std::vector<ParseError> errors_;

    //-- token stream helpers --
    const Token& peek(size_t offset = 0) const;
    const Token& previous() const;
    bool isAtEnd() const;
    bool check(TokenType type, size_t offset = 0) const;
    const Token& advance();
    bool match(TokenType type);
    const Token& expect(TokenType type, const std::string& message);
    [[noreturn]] void error(const std::string& message);
    [[noreturn]] void error(const Token& at, const std::string& message);
    void synchronize();

    //-- statements --
    std::unique_ptr<Stmt> parseStatement();
    std::unique_ptr<Stmt> parseImportDecl();
    std::string parseImportPath();
    std::unique_ptr<Stmt> parseEnumDecl();
    std::unique_ptr<Stmt> parseClassDecl();
    ClassMemberNode parseClassMember();
    std::unique_ptr<Stmt> parseFnDecl();
    std::unique_ptr<Stmt> parseVarDecl();
    std::unique_ptr<Stmt> parseSwitchStmt();
    std::unique_ptr<Stmt> parseWhileStmt();
    std::unique_ptr<Stmt> parseForStmt();
    std::unique_ptr<Stmt> parseReturnStmt();
    std::unique_ptr<Stmt> parseBreakStmt();
    std::unique_ptr<Stmt> parseExprStmt();
    std::vector<std::unique_ptr<Stmt>> parseBlock();
    std::vector<Param> parseParamList();
    Param parseParam();

    //-- lookahead helpers for the VarDecl-vs-ExprStmt / ForIn-vs-ForC ambiguity --
    bool startsTypedDecl(size_t offset = 0) const;
    bool isPrimitiveTypeToken(TokenType type) const;

    //-- types --
    std::unique_ptr<TypeNode> parseBaseType();          //BaseType ['?'] only
    std::unique_ptr<TypeNode> parseStandaloneType();     //BaseType ['?'] ['[]'], for generics/catch

    //-- expressions, in precedence order --
    std::unique_ptr<Expr> parseExpr();
    std::unique_ptr<Expr> parseAssignment();
    std::unique_ptr<Expr> parseLogical();
    std::unique_ptr<Expr> parseRelational();
    std::unique_ptr<Expr> parseAdditive();
    std::unique_ptr<Expr> parseMultiplicative();
    std::unique_ptr<Expr> parseExponent();
    std::unique_ptr<Expr> parseUnary();
    std::unique_ptr<Expr> parsePostfix();
    std::unique_ptr<Expr> parsePrimary();
    std::vector<Arg> parseArgList();
    std::unique_ptr<Expr> parseArrayLiteral();
    std::unique_ptr<Expr> parseMapLiteral();
    std::unique_ptr<Expr> parseLambda();
    std::unique_ptr<Expr> parseIfExpr();
    std::unique_ptr<Expr> parseTryExpr();
    std::unique_ptr<Expr> parseTemplateString();
    bool looksLikeLambdaParams() const;

    std::unique_ptr<Expr> makeExpr(ExprKind kind, const Token& at);
};

}//namespace zinc
