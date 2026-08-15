#include "parser/Parser.h"

namespace zinc{

Parser::Parser(std::vector<Token> tokens): tokens_(std::move(tokens)){}

//== token stream helpers ====================================================

const Token& Parser::peek(size_t offset) const{
    size_t idx = pos_ + offset;
    if(idx >= tokens_.size()){
        return tokens_.back(); //Eof is always last
    }
    return tokens_[idx];
}

const Token& Parser::previous() const{
    return tokens_[pos_ - 1];
}

bool Parser::isAtEnd() const{
    return peek().type == TokenType::Eof;
}

bool Parser::check(TokenType type, size_t offset) const{
    return peek(offset).type == type;
}

const Token& Parser::advance(){
    if(!isAtEnd()){
        pos_++;
    }
    return previous();
}

bool Parser::match(TokenType type){
    if(check(type)){
        advance();
        return true;
    }
    return false;
}

const Token& Parser::expect(TokenType type, const std::string& message){
    if(check(type)){
        return advance();
    }
    error(peek(), message);
}

void Parser::error(const std::string& message){
    error(peek(), message);
}

void Parser::error(const Token& at, const std::string& message){
    errors_.push_back(ParseError{message, at.line, at.column});
    throw ParseException{};
}

void Parser::synchronize(){
    advance(); //skip the token that caused the error
    while(!isAtEnd()){
        if(previous().type == TokenType::Semicolon){
            return;
        }
        switch(peek().type){
            case TokenType::Import:
            case TokenType::Enum:
            case TokenType::Class:
            case TokenType::Fn:
            case TokenType::Let:
            case TokenType::Const:
            case TokenType::If:
            case TokenType::Switch:
            case TokenType::Try:
            case TokenType::While:
            case TokenType::For:
            case TokenType::Return:
            case TokenType::Break:
                return;
            default:
                break;
        }
        advance();
    }
}

std::unique_ptr<Expr> Parser::makeExpr(ExprKind kind, const Token& at){
    auto node = std::make_unique<Expr>();
    node->kind = kind;
    node->line = at.line;
    node->column = at.column;
    return node;
}

//== program / statements ====================================================

Program Parser::parseProgram(){
    Program program;
    while(!isAtEnd()){
        try{
            auto stmt = parseStatement();
            if(stmt){
                program.push_back(std::move(stmt));
            }
        }catch(const ParseException&){
            synchronize();
        }
    }
    return program;
}

bool Parser::isPrimitiveTypeToken(TokenType type) const{
    return type == TokenType::Int || type == TokenType::Float
        || type == TokenType::Bool || type == TokenType::Str;
}

bool Parser::startsTypedDecl(size_t offset) const{
    TokenType t = peek(offset).type;
    if(t == TokenType::Let || t == TokenType::Const){
        return true;
    }
    if(isPrimitiveTypeToken(t) || t == TokenType::Map){
        return true;
    }
    if(t == TokenType::Identifier){
        //"Foo x" / "Foo? x" is a typed decl; a bare "Foo" starts an expression
        size_t idx = offset + 1;
        if(peek(idx).type == TokenType::Question){
            idx++;
        }
        return peek(idx).type == TokenType::Identifier;
    }
    return false;
}

std::unique_ptr<Stmt> Parser::parseStatement(){
    if(check(TokenType::Import)) return parseImportDecl();
    if(check(TokenType::Enum)) return parseEnumDecl();
    if(check(TokenType::Class)) return parseClassDecl();
    if(check(TokenType::Fn)) return parseFnDecl();
    if(check(TokenType::Switch)) return parseSwitchStmt();
    if(check(TokenType::While)) return parseWhileStmt();
    if(check(TokenType::For)) return parseForStmt();
    if(check(TokenType::Return)) return parseReturnStmt();
    if(check(TokenType::Break)) return parseBreakStmt();

    //IfExpr and TryExpr are expressions that also stand alone as statements
    //(no trailing ';' per the grammar), so parse the expr and wrap it
    if(check(TokenType::If) || check(TokenType::Try)){
        Token start = peek();
        auto expr = check(TokenType::If) ? parseIfExpr() : parseTryExpr();
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = StmtKind::ExprStmt;
        stmt->line = start.line;
        stmt->column = start.column;
        stmt->expr = std::move(expr);
        return stmt;
    }

    if(startsTypedDecl()){
        auto decl = parseVarDecl();
        expect(TokenType::Semicolon, "expected ';' after variable declaration");
        return decl;
    }

    return parseExprStmt();
}

std::unique_ptr<Stmt> Parser::parseImportDecl(){
    Token start = advance(); //'import'
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = StmtKind::Import;
    stmt->line = start.line;
    stmt->column = start.column;

    if(match(TokenType::LBrace)){
        stmt->isNamedImport = true;
        stmt->importNames.push_back(expect(TokenType::Identifier, "expected identifier in import list").lexeme);
        while(match(TokenType::Comma)){
            stmt->importNames.push_back(expect(TokenType::Identifier, "expected identifier in import list").lexeme);
        }
        expect(TokenType::RBrace, "expected '}' after import list");
        expect(TokenType::From, "expected 'from' after import list");
        stmt->importPath = parseImportPath();
    }else{
        stmt->importPath = parseImportPath();
    }
    expect(TokenType::Semicolon, "expected ';' after import");
    return stmt;
}

std::string Parser::parseImportPath(){
    if(check(TokenType::StringLiteral)){
        return advance().lexeme;
    }
    std::string path = expect(TokenType::Identifier, "expected import path").lexeme;
    while(match(TokenType::Dot)){
        path += ".";
        path += expect(TokenType::Identifier, "expected identifier after '.' in import path").lexeme;
    }
    return path;
}

std::unique_ptr<Stmt> Parser::parseEnumDecl(){
    Token start = advance(); //'enum'
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = StmtKind::EnumDecl;
    stmt->line = start.line;
    stmt->column = start.column;
    stmt->name = expect(TokenType::Identifier, "expected enum name").lexeme;
    expect(TokenType::LBrace, "expected '{' after enum name");
    stmt->enumVariants.push_back(expect(TokenType::Identifier, "expected enum variant").lexeme);
    while(match(TokenType::Comma)){
        if(check(TokenType::RBrace)) break; //allow trailing comma
        stmt->enumVariants.push_back(expect(TokenType::Identifier, "expected enum variant").lexeme);
    }
    expect(TokenType::RBrace, "expected '}' after enum variants");
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseClassDecl(){
    Token start = advance(); //'class'
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = StmtKind::ClassDecl;
    stmt->line = start.line;
    stmt->column = start.column;
    stmt->name = expect(TokenType::Identifier, "expected class name").lexeme;
    if(match(TokenType::Extends)){
        stmt->baseClassName = expect(TokenType::Identifier, "expected base class name after 'extends'").lexeme;
    }
    expect(TokenType::LBrace, "expected '{' to start class body");
    while(!check(TokenType::RBrace) && !isAtEnd()){
        try{
            stmt->members.push_back(parseClassMember());
        }catch(const ParseException&){
            synchronize();
        }
    }
    expect(TokenType::RBrace, "expected '}' to close class body");
    return stmt;
}

ClassMemberNode Parser::parseClassMember(){
    ClassMemberNode member;
    member.isPriv = match(TokenType::Priv);

    if(check(TokenType::Fn)){
        advance(); //'fn'
        if(check(TokenType::Init)){
            advance(); //'init'
            member.kind = ClassMemberNode::Kind::Ctor;
        }else{
            member.kind = ClassMemberNode::Kind::Fn;
            member.name = expect(TokenType::Identifier, "expected method name").lexeme;
        }
        expect(TokenType::LParen, "expected '(' after method/constructor name");
        if(!check(TokenType::RParen)){
            member.params = parseParamList();
        }
        expect(TokenType::RParen, "expected ')' after parameters");
        member.body = parseBlock();
        return member;
    }

    //MemberVarDecl
    member.kind = ClassMemberNode::Kind::Var;
    if(match(TokenType::Let)){
        member.isConst = false;
    }else if(match(TokenType::Const)){
        member.isConst = true;
    }else{
        member.type = parseBaseType();
    }
    member.name = expect(TokenType::Identifier, "expected field name").lexeme;
    if(match(TokenType::LBracket)){
        expect(TokenType::RBracket, "expected ']' after '[' in array field declaration");
        member.isArray = true;
    }
    expect(TokenType::Equal, "expected '=' in field declaration");
    member.initExpr = parseExpr();
    expect(TokenType::Semicolon, "expected ';' after field declaration");
    return member;
}

std::unique_ptr<Stmt> Parser::parseFnDecl(){
    Token start = advance(); //'fn'
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = StmtKind::FnDecl;
    stmt->line = start.line;
    stmt->column = start.column;
    stmt->name = expect(TokenType::Identifier, "expected function name").lexeme;
    expect(TokenType::LParen, "expected '(' after function name");
    if(!check(TokenType::RParen)){
        stmt->params = parseParamList();
    }
    expect(TokenType::RParen, "expected ')' after parameters");
    stmt->body = parseBlock();
    return stmt;
}

std::vector<Param> Parser::parseParamList(){
    std::vector<Param> params;
    params.push_back(parseParam());
    while(match(TokenType::Comma)){
        params.push_back(parseParam());
    }
    return params;
}

Param Parser::parseParam(){
    Param param;
    param.isRef = match(TokenType::Ref);
    param.type = parseBaseType();
    param.name = expect(TokenType::Identifier, "expected parameter name").lexeme;
    if(match(TokenType::LBracket)){
        expect(TokenType::RBracket, "expected ']' after '[' in array parameter");
        param.isArray = true;
    }
    return param;
}

std::unique_ptr<Stmt> Parser::parseVarDecl(){
    Token start = peek();
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = StmtKind::VarDecl;
    stmt->line = start.line;
    stmt->column = start.column;

    if(match(TokenType::Let)){
        stmt->isConst = false;
    }else if(match(TokenType::Const)){
        stmt->isConst = true;
    }else{
        stmt->type = parseBaseType();
    }
    stmt->name = expect(TokenType::Identifier, "expected variable name").lexeme;
    if(match(TokenType::LBracket)){
        expect(TokenType::RBracket, "expected ']' after '[' in array declaration");
        stmt->isArray = true;
    }
    expect(TokenType::Equal, "expected '=' in variable declaration");
    stmt->initExpr = parseExpr();
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseSwitchStmt(){
    Token start = advance(); //'switch'
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = StmtKind::Switch;
    stmt->line = start.line;
    stmt->column = start.column;
    stmt->subject = parseExpr();
    expect(TokenType::LBrace, "expected '{' after switch subject");

    while(check(TokenType::Case)){
        advance();
        CaseClauseNode clause;
        clause.value = parseExpr();
        expect(TokenType::Colon, "expected ':' after case value");
        while(!check(TokenType::Case) && !check(TokenType::Default) && !check(TokenType::RBrace) && !isAtEnd()){
            clause.body.push_back(parseStatement());
        }
        stmt->cases.push_back(std::move(clause));
    }
    if(match(TokenType::Default)){
        stmt->hasDefault = true;
        expect(TokenType::Colon, "expected ':' after 'default'");
        while(!check(TokenType::RBrace) && !isAtEnd()){
            stmt->defaultBody.push_back(parseStatement());
        }
    }
    expect(TokenType::RBrace, "expected '}' to close switch");
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseWhileStmt(){
    Token start = advance(); //'while'
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = StmtKind::While;
    stmt->line = start.line;
    stmt->column = start.column;
    stmt->condition = parseExpr();
    stmt->bodyBlock = parseBlock();
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseForStmt(){
    Token start = advance(); //'for'

    //Identifier 'in' Expr signals for-in; anything else falls through to the
    //C-style VarDecl ';' Expr ';' Expr form
    if(check(TokenType::Identifier) && check(TokenType::In, 1)){
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = StmtKind::ForIn;
        stmt->line = start.line;
        stmt->column = start.column;
        stmt->loopVarName = advance().lexeme; //identifier
        advance(); //'in'
        stmt->iterable = parseExpr();
        stmt->bodyBlock = parseBlock();
        return stmt;
    }

    auto stmt = std::make_unique<Stmt>();
    stmt->kind = StmtKind::ForC;
    stmt->line = start.line;
    stmt->column = start.column;
    stmt->forInit = parseVarDecl();
    expect(TokenType::Semicolon, "expected ';' after for-loop initializer");
    stmt->condition = parseExpr();
    expect(TokenType::Semicolon, "expected ';' after for-loop condition");
    stmt->forIncrement = parseExpr();
    stmt->bodyBlock = parseBlock();
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseReturnStmt(){
    Token start = advance(); //'return'
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = StmtKind::Return;
    stmt->line = start.line;
    stmt->column = start.column;
    if(!check(TokenType::Semicolon)){
        stmt->returnValue = parseExpr();
    }
    expect(TokenType::Semicolon, "expected ';' after return statement");
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseBreakStmt(){
    Token start = advance(); //'break'
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = StmtKind::Break;
    stmt->line = start.line;
    stmt->column = start.column;
    expect(TokenType::Semicolon, "expected ';' after 'break'");
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseExprStmt(){
    Token start = peek();
    auto expr = parseExpr();
    expect(TokenType::Semicolon, "expected ';' after expression");
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = StmtKind::ExprStmt;
    stmt->line = start.line;
    stmt->column = start.column;
    stmt->expr = std::move(expr);
    return stmt;
}

std::vector<std::unique_ptr<Stmt>> Parser::parseBlock(){
    expect(TokenType::LBrace, "expected '{'");
    std::vector<std::unique_ptr<Stmt>> stmts;
    while(!check(TokenType::RBrace) && !isAtEnd()){
        stmts.push_back(parseStatement());
    }
    expect(TokenType::RBrace, "expected '}'");
    return stmts;
}

//== types ====================================================================

std::unique_ptr<TypeNode> Parser::parseBaseType(){
    auto node = std::make_unique<TypeNode>();
    if(check(TokenType::Map)){
        advance();
        node->isMap = true;
        expect(TokenType::Less, "expected '<' after 'Map'");
        node->mapKeyType = parseStandaloneType();
        expect(TokenType::Comma, "expected ',' between Map key and value types");
        node->mapValueType = parseStandaloneType();
        expect(TokenType::Greater, "expected '>' after Map type arguments");
    }else if(isPrimitiveTypeToken(peek().type)){
        node->name = advance().lexeme;
    }else if(check(TokenType::Identifier)){
        node->name = advance().lexeme;
    }else{
        error("expected a type");
    }
    if(match(TokenType::Question)){
        node->nullable = true;
    }
    return node;
}

std::unique_ptr<TypeNode> Parser::parseStandaloneType(){
    auto node = parseBaseType();
    if(match(TokenType::LBracket)){
        expect(TokenType::RBracket, "expected ']' after '[' in type");
        node->isArray = true;
    }
    return node;
}

//== expressions, in precedence order ========================================

std::unique_ptr<Expr> Parser::parseExpr(){
    return parseAssignment();
}

std::unique_ptr<Expr> Parser::parseAssignment(){
    auto left = parseLogical();
    if(check(TokenType::Equal) || check(TokenType::PlusEqual) || check(TokenType::MinusEqual)
       || check(TokenType::StarEqual) || check(TokenType::SlashEqual)){
        Token opTok = advance();
        auto right = parseExpr(); //right-associative
        auto node = makeExpr(ExprKind::Assignment, opTok);
        node->text = opTok.lexeme;
        node->target = std::move(left);
        node->right = std::move(right);
        return node;
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseLogical(){
    auto left = parseRelational();
    while(check(TokenType::AmpAmp) || check(TokenType::PipePipe)){
        Token opTok = advance();
        auto right = parseRelational();
        auto node = makeExpr(ExprKind::Binary, opTok);
        node->text = opTok.lexeme;
        node->target = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseRelational(){
    auto left = parseAdditive();
    while(check(TokenType::EqualEqual) || check(TokenType::BangEqual) || check(TokenType::Less)
          || check(TokenType::Greater) || check(TokenType::LessEqual) || check(TokenType::GreaterEqual)){
        Token opTok = advance();
        auto right = parseAdditive();
        auto node = makeExpr(ExprKind::Binary, opTok);
        node->text = opTok.lexeme;
        node->target = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseAdditive(){
    auto left = parseMultiplicative();
    while(check(TokenType::Plus) || check(TokenType::Minus)){
        Token opTok = advance();
        auto right = parseMultiplicative();
        auto node = makeExpr(ExprKind::Binary, opTok);
        node->text = opTok.lexeme;
        node->target = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseMultiplicative(){
    auto left = parseExponent();
    while(check(TokenType::Star) || check(TokenType::Slash) || check(TokenType::Percent)){
        Token opTok = advance();
        auto right = parseExponent();
        auto node = makeExpr(ExprKind::Binary, opTok);
        node->text = opTok.lexeme;
        node->target = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseExponent(){
    auto left = parseUnary();
    while(check(TokenType::StarStar)){
        Token opTok = advance();
        auto right = parseUnary();
        auto node = makeExpr(ExprKind::Binary, opTok);
        node->text = opTok.lexeme;
        node->target = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseUnary(){
    if(check(TokenType::Bang) || check(TokenType::Minus)
       || check(TokenType::PlusPlus) || check(TokenType::MinusMinus)){
        Token opTok = advance();
        bool isUpdate = (opTok.type == TokenType::PlusPlus || opTok.type == TokenType::MinusMinus);
        auto node = makeExpr(isUpdate ? ExprKind::Update : ExprKind::Unary, opTok);
        node->text = opTok.lexeme;
        node->isPrefix = true;
        node->target = parseUnary();
        return node;
    }
    return parsePostfix();
}

std::unique_ptr<Expr> Parser::parsePostfix(){
    auto expr = parsePrimary();
    while(true){
        if(check(TokenType::PlusPlus) || check(TokenType::MinusMinus)){
            Token opTok = advance();
            auto node = makeExpr(ExprKind::Update, opTok);
            node->text = opTok.lexeme;
            node->isPrefix = false;
            node->target = std::move(expr);
            expr = std::move(node);
        }else if(check(TokenType::LParen)){
            Token opTok = advance();
            auto node = makeExpr(ExprKind::Call, opTok);
            node->target = std::move(expr);
            if(!check(TokenType::RParen)){
                node->args = parseArgList();
            }
            expect(TokenType::RParen, "expected ')' after call arguments");
            expr = std::move(node);
        }else if(check(TokenType::LBracket)){
            Token opTok = advance();
            auto node = makeExpr(ExprKind::Index, opTok);
            node->target = std::move(expr);
            node->right = parseExpr();
            expect(TokenType::RBracket, "expected ']' after index expression");
            expr = std::move(node);
        }else if(check(TokenType::Dot)){
            advance();
            Token nameTok = expect(TokenType::Identifier, "expected property name after '.'");
            auto node = makeExpr(ExprKind::Property, nameTok);
            node->target = std::move(expr);
            node->text = nameTok.lexeme;
            expr = std::move(node);
        }else{
            break;
        }
    }
    return expr;
}

std::vector<Arg> Parser::parseArgList(){
    std::vector<Arg> args;
    auto parseOne = [&](){
        Arg arg;
        arg.isRef = match(TokenType::Ref);
        arg.value = parseExpr();
        args.push_back(std::move(arg));
    };
    parseOne();
    while(match(TokenType::Comma)){
        parseOne();
    }
    return args;
}

bool Parser::looksLikeLambdaParams() const{
    auto at = [&](size_t idx) -> const Token&{
        if(idx >= tokens_.size()) return tokens_.back();
        return tokens_[idx];
    };
    size_t i = pos_ + 1; //tokens_[pos_] is the '('
    if(at(i).type == TokenType::RParen){
        return at(i + 1).type == TokenType::FatArrow;
    }
    while(true){
        if(at(i).type != TokenType::Identifier) return false;
        i++;
        if(at(i).type == TokenType::RParen){
            return at(i + 1).type == TokenType::FatArrow;
        }
        if(at(i).type != TokenType::Comma) return false;
        i++;
    }
}

std::unique_ptr<Expr> Parser::parsePrimary(){
    Token tok = peek();

    if(check(TokenType::IntLiteral)){
        advance();
        auto node = makeExpr(ExprKind::IntLiteral, tok);
        node->text = tok.lexeme;
        return node;
    }
    if(check(TokenType::FloatLiteral)){
        advance();
        auto node = makeExpr(ExprKind::FloatLiteral, tok);
        node->text = tok.lexeme;
        return node;
    }
    if(check(TokenType::StringLiteral)){
        advance();
        auto node = makeExpr(ExprKind::StringLiteral, tok);
        node->text = tok.lexeme;
        return node;
    }
    if(check(TokenType::BoolLiteral)){
        advance();
        auto node = makeExpr(ExprKind::BoolLiteral, tok);
        node->boolValue = (tok.lexeme == "true");
        return node;
    }
    if(check(TokenType::NullLiteral)){
        advance();
        return makeExpr(ExprKind::NullLiteral, tok);
    }
    if(check(TokenType::This)){
        advance();
        return makeExpr(ExprKind::This, tok);
    }
    if(check(TokenType::TemplateStart)){
        return parseTemplateString();
    }
    if(check(TokenType::Identifier)){
        advance();
        auto node = makeExpr(ExprKind::Identifier, tok);
        node->text = tok.lexeme;
        return node;
    }
    if(check(TokenType::LBracket)){
        return parseArrayLiteral();
    }
    if(check(TokenType::LBrace)){
        return parseMapLiteral();
    }
    if(check(TokenType::If)){
        return parseIfExpr();
    }
    if(check(TokenType::Try)){
        return parseTryExpr();
    }
    if(check(TokenType::LParen)){
        if(looksLikeLambdaParams()){
            return parseLambda();
        }
        advance(); //'('
        auto inner = parseExpr();
        expect(TokenType::RParen, "expected ')' after expression");
        return inner;
    }

    error(tok, "expected an expression");
}

std::unique_ptr<Expr> Parser::parseArrayLiteral(){
    Token start = advance(); //'['
    auto node = makeExpr(ExprKind::ArrayLiteral, start);
    if(!check(TokenType::RBracket)){
        node->elements.push_back(parseExpr());
        while(match(TokenType::Comma)){
            if(check(TokenType::RBracket)) break; //trailing comma
            node->elements.push_back(parseExpr());
        }
    }
    expect(TokenType::RBracket, "expected ']' after array elements");
    return node;
}

std::unique_ptr<Expr> Parser::parseMapLiteral(){
    Token start = advance(); //'{'
    auto node = makeExpr(ExprKind::MapLiteral, start);
    auto parseEntry = [&](){
        Token keyTok = expect(TokenType::StringLiteral, "expected string literal key in map entry");
        expect(TokenType::Colon, "expected ':' after map key");
        MapEntryNode entry;
        entry.key = keyTok.lexeme;
        entry.value = parseExpr();
        node->mapEntries.push_back(std::move(entry));
    };
    if(!check(TokenType::RBrace)){
        parseEntry();
        while(match(TokenType::Comma)){
            if(check(TokenType::RBrace)) break;
            parseEntry();
        }
    }
    expect(TokenType::RBrace, "expected '}' after map entries");
    return node;
}

std::unique_ptr<Expr> Parser::parseLambda(){
    Token start = advance(); //'('
    auto node = makeExpr(ExprKind::Lambda, start);
    if(!check(TokenType::RParen)){
        node->lambdaParams.push_back(expect(TokenType::Identifier, "expected parameter name").lexeme);
        while(match(TokenType::Comma)){
            node->lambdaParams.push_back(expect(TokenType::Identifier, "expected parameter name").lexeme);
        }
    }
    expect(TokenType::RParen, "expected ')' after lambda parameters");
    expect(TokenType::FatArrow, "expected '=>' after lambda parameters");
    if(check(TokenType::LBrace)){
        node->isBlockBody = true;
        node->block = parseBlock();
    }else{
        node->bodyExpr = parseExpr();
    }
    return node;
}

std::unique_ptr<Expr> Parser::parseIfExpr(){
    Token start = advance(); //'if'
    auto node = makeExpr(ExprKind::If, start);
    node->condition = parseExpr();
    node->block = parseBlock();
    if(match(TokenType::Else)){
        node->hasElse = true;
        if(check(TokenType::If)){
            node->elseIsIf = true;
            node->elseIf = parseIfExpr();
        }else{
            node->elseIsIf = false;
            node->elseBlock = parseBlock();
        }
    }
    return node;
}

std::unique_ptr<Expr> Parser::parseTryExpr(){
    Token start = advance(); //'try'
    auto node = makeExpr(ExprKind::Try, start);
    node->block = parseBlock();
    expect(TokenType::Catch, "expected 'catch' after try block");
    expect(TokenType::LParen, "expected '(' after 'catch'");

    //a bare identifier immediately before ')' is just the catch variable;
    //anything else there is a Type preceding it
    if(check(TokenType::Identifier) && check(TokenType::RParen, 1)){
        node->catchHasType = false;
    }else{
        node->catchHasType = true;
        node->catchType = parseStandaloneType();
    }
    node->catchName = expect(TokenType::Identifier, "expected identifier in catch clause").lexeme;
    expect(TokenType::RParen, "expected ')' after catch clause");
    node->catchBlock = parseBlock();
    return node;
}

std::unique_ptr<Expr> Parser::parseTemplateString(){
    Token start = advance(); //TemplateStart
    auto node = makeExpr(ExprKind::TemplateString, start);
    while(!check(TokenType::TemplateEnd) && !isAtEnd()){
        if(check(TokenType::TemplateStringPart)){
            Token partTok = advance();
            TemplatePart part;
            part.isExpr = false;
            part.text = partTok.lexeme;
            node->templateParts.push_back(std::move(part));
        }else if(check(TokenType::TemplateExprStart)){
            advance();
            TemplatePart part;
            part.isExpr = true;
            part.expr = parseExpr();
            expect(TokenType::TemplateExprEnd, "expected '}' to close template interpolation");
            node->templateParts.push_back(std::move(part));
        }else{
            error("malformed template string");
        }
    }
    expect(TokenType::TemplateEnd, "expected closing '`' for template string");
    return node;
}

}//namespace zinc
