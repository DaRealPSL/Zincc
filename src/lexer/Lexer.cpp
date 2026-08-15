#include "lexer/Lexer.h"

#include <cctype>

namespace zinc{

Lexer::Lexer(std::string source): source_(std::move(source)){
    modeStack_.push_back({Mode::Normal, 0});
}

bool Lexer::isAtEnd() const{
    return pos_ >= source_.size();
}

char Lexer::peek(size_t offset) const{
    size_t idx = pos_ + offset;
    if(idx >= source_.size()){
        return '\0';
    }
    return source_[idx];
}

char Lexer::advance(){
    char c = source_[pos_++];
    if(c == '\n'){
        line_++;
        column_ = 1;
    }else{
        column_++;
    }
    return c;
}

bool Lexer::match(char expected){
    if(isAtEnd() || peek() != expected){
        return false;
    }
    advance();
    return true;
}

void Lexer::skipWhitespaceAndComments(){
    while(!isAtEnd()){
        char c = peek();

        if(c == ' ' || c == '\t' || c == '\r' || c == '\n'){
            advance();
            continue;
        }

        if(c == '/' && peek(1) == '/'){
            while(!isAtEnd() && peek() != '\n'){
                advance();
            }
            continue;
        }

        if(c == '/' && peek(1) == '*'){
            size_t startLine = line_, startCol = column_;
            advance();
            advance();
            bool closed = false;
            while(!isAtEnd()){
                if(peek() == '*' && peek(1) == '/'){
                    advance();
                    advance();
                    closed = true;
                    break;
                }
                advance();
            }
            if(!closed){
                reportError("unterminated block comment", startLine, startCol);
            }
            continue;
        }

        break;
    }
}

Token Lexer::makeToken(TokenType type, const std::string& lexeme, size_t line, size_t column){
    return Token{type, lexeme, line, column};
}

void Lexer::reportError(const std::string& message, size_t line, size_t column){
    errors_.push_back(LexError{message, line, column});
}

std::string Lexer::lexEscapeSequence(char /*terminator*/){
    size_t errLine = line_, errCol = column_;
    advance(); //consume the backslash
    if(isAtEnd()){
        reportError("unterminated escape sequence", errLine, errCol);
        return "";
    }
    char c = advance();
    switch(c){
        case '"': return "\"";
        case '`': return "`";
        case '\\': return "\\";
        case 'n': return "\n";
        case 't': return "\t";
        case 'r': return "\r";
        case '0': return std::string(1, '\0');
        case '{': return "{";
        default:
            reportError(std::string("unknown escape sequence '\\") + c + "'", errLine, errCol);
            return std::string(1, c);
    }
}

Token Lexer::lexIdentifierOrKeyword(){
    size_t startLine = line_, startCol = column_;
    std::string text;
    while(!isAtEnd() && (peek() == '_' || std::isalnum(static_cast<unsigned char>(peek())))){
        text += advance();
    }
    const TokenType* kw = lookupKeyword(text);
    if(kw != nullptr){
        return makeToken(*kw, text, startLine, startCol);
    }
    return makeToken(TokenType::Identifier, text, startLine, startCol);
}

Token Lexer::lexNumber(){
    size_t startLine = line_, startCol = column_;
    std::string text;
    while(!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))){
        text += advance();
    }

    bool isFloat = false;
    //FLOAT_LITERAL requires a digit on both sides of the dot, so only
    //consume the dot as part of the number when one is actually there
    if(!isAtEnd() && peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))){
        isFloat = true;
        text += advance(); //the dot
        while(!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))){
            text += advance();
        }
    }

    return makeToken(isFloat ? TokenType::FloatLiteral : TokenType::IntLiteral, text, startLine, startCol);
}

Token Lexer::lexStringLiteral(){
    size_t startLine = line_, startCol = column_;
    advance(); //opening quote

    std::string value;
    while(!isAtEnd() && peek() != '"' && peek() != '\n'){
        if(peek() == '\\'){
            value += lexEscapeSequence('"');
        }else{
            value += advance();
        }
    }

    if(isAtEnd() || peek() != '"'){
        reportError("unterminated string literal", startLine, startCol);
        return makeToken(TokenType::Invalid, value, startLine, startCol);
    }
    advance(); //closing quote

    return makeToken(TokenType::StringLiteral, value, startLine, startCol);
}

Token Lexer::nextTemplateStringPart(){
    size_t startLine = line_, startCol = column_;

    if(isAtEnd()){
        reportError("unterminated template string", startLine, startCol);
        return makeToken(TokenType::Eof, "", startLine, startCol);
    }

    if(peek() == '`'){
        advance();
        modeStack_.pop_back(); //leave InTemplate
        return makeToken(TokenType::TemplateEnd, "`", startLine, startCol);
    }

    if(peek() == '{'){
        advance();
        modeStack_.push_back({Mode::InTemplateExpr, 0}); //enter the interpolated expr
        return makeToken(TokenType::TemplateExprStart, "{", startLine, startCol);
    }

    std::string value;
    while(!isAtEnd() && peek() != '`' && peek() != '{'){
        if(peek() == '\\'){
            value += lexEscapeSequence('`');
        }else{
            value += advance();
        }
    }

    if(isAtEnd()){
        reportError("unterminated template string", startLine, startCol);
        return makeToken(TokenType::Eof, "", startLine, startCol);
    }

    return makeToken(TokenType::TemplateStringPart, value, startLine, startCol);
}

Token Lexer::nextNormalToken(){
    skipWhitespaceAndComments();
    size_t startLine = line_, startCol = column_;

    if(isAtEnd()){
        if(modeStack_.size() > 1){
            reportError("unterminated template string", startLine, startCol);
        }
        return makeToken(TokenType::Eof, "", startLine, startCol);
    }

    char c = peek();

    if(c == '`'){
        advance();
        modeStack_.push_back({Mode::InTemplate, 0});
        return makeToken(TokenType::TemplateStart, "`", startLine, startCol);
    }

    if(c == '_' || std::isalpha(static_cast<unsigned char>(c))){
        return lexIdentifierOrKeyword();
    }

    if(std::isdigit(static_cast<unsigned char>(c))){
        return lexNumber();
    }

    if(c == '"'){
        return lexStringLiteral();
    }

    advance();
    switch(c){
        case '{':
            //only a template-expr frame needs to track nested braces, so it
            //can tell "the } that closes me" from "a } that belongs to a
            //map literal or block inside me"
            if(modeStack_.back().mode == Mode::InTemplateExpr){
                modeStack_.back().braceDepth++;
            }
            return makeToken(TokenType::LBrace, "{", startLine, startCol);
        case '}':
            if(modeStack_.back().mode == Mode::InTemplateExpr){
                if(modeStack_.back().braceDepth == 0){
                    modeStack_.pop_back(); //back to the enclosing InTemplate frame
                    return makeToken(TokenType::TemplateExprEnd, "}", startLine, startCol);
                }
                modeStack_.back().braceDepth--;
            }
            return makeToken(TokenType::RBrace, "}", startLine, startCol);
        case '[': return makeToken(TokenType::LBracket, "[", startLine, startCol);
        case ']': return makeToken(TokenType::RBracket, "]", startLine, startCol);
        case '(': return makeToken(TokenType::LParen, "(", startLine, startCol);
        case ')': return makeToken(TokenType::RParen, ")", startLine, startCol);
        case ':': return makeToken(TokenType::Colon, ":", startLine, startCol);
        case ';': return makeToken(TokenType::Semicolon, ";", startLine, startCol);
        case ',': return makeToken(TokenType::Comma, ",", startLine, startCol);
        case '.': return makeToken(TokenType::Dot, ".", startLine, startCol);
        case '?': return makeToken(TokenType::Question, "?", startLine, startCol);

        case '+':
            if(match('+')) return makeToken(TokenType::PlusPlus, "++", startLine, startCol);
            if(match('=')) return makeToken(TokenType::PlusEqual, "+=", startLine, startCol);
            return makeToken(TokenType::Plus, "+", startLine, startCol);
        case '-':
            if(match('-')) return makeToken(TokenType::MinusMinus, "--", startLine, startCol);
            if(match('=')) return makeToken(TokenType::MinusEqual, "-=", startLine, startCol);
            return makeToken(TokenType::Minus, "-", startLine, startCol);
        case '*':
            if(match('*')) return makeToken(TokenType::StarStar, "**", startLine, startCol);
            if(match('=')) return makeToken(TokenType::StarEqual, "*=", startLine, startCol);
            return makeToken(TokenType::Star, "*", startLine, startCol);
        case '/':
            if(match('=')) return makeToken(TokenType::SlashEqual, "/=", startLine, startCol);
            return makeToken(TokenType::Slash, "/", startLine, startCol);
        case '%':
            return makeToken(TokenType::Percent, "%", startLine, startCol);
        case '=':
            if(match('=')) return makeToken(TokenType::EqualEqual, "==", startLine, startCol);
            if(match('>')) return makeToken(TokenType::FatArrow, "=>", startLine, startCol);
            return makeToken(TokenType::Equal, "=", startLine, startCol);
        case '!':
            if(match('=')) return makeToken(TokenType::BangEqual, "!=", startLine, startCol);
            return makeToken(TokenType::Bang, "!", startLine, startCol);
        case '<':
            if(match('=')) return makeToken(TokenType::LessEqual, "<=", startLine, startCol);
            return makeToken(TokenType::Less, "<", startLine, startCol);
        case '>':
            if(match('=')) return makeToken(TokenType::GreaterEqual, ">=", startLine, startCol);
            return makeToken(TokenType::Greater, ">", startLine, startCol);
        case '&':
            if(match('&')) return makeToken(TokenType::AmpAmp, "&&", startLine, startCol);
            break;
        case '|':
            if(match('|')) return makeToken(TokenType::PipePipe, "||", startLine, startCol);
            break;
    }

    reportError(std::string("unexpected character '") + c + "'", startLine, startCol);
    return makeToken(TokenType::Invalid, std::string(1, c), startLine, startCol);
}

std::vector<Token> Lexer::tokenize(){
    std::vector<Token> tokens;
    while(true){
        Mode mode = modeStack_.back().mode;
        Token tok = (mode == Mode::InTemplate) ? nextTemplateStringPart() : nextNormalToken();
        tokens.push_back(tok);
        if(tok.type == TokenType::Eof){
            break;
        }
    }
    return tokens;
}

}//namespace zinc
