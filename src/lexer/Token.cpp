#include "lexer/Token.h"

#include <unordered_map>

namespace zinc{

const char* tokenTypeName(TokenType type){
    switch(type){
        case TokenType::IntLiteral: return "IntLiteral";
        case TokenType::FloatLiteral: return "FloatLiteral";
        case TokenType::StringLiteral: return "StringLiteral";
        case TokenType::BoolLiteral: return "BoolLiteral";
        case TokenType::NullLiteral: return "NullLiteral";
        case TokenType::Identifier: return "Identifier";

        case TokenType::TemplateStart: return "TemplateStart";
        case TokenType::TemplateStringPart: return "TemplateStringPart";
        case TokenType::TemplateExprStart: return "TemplateExprStart";
        case TokenType::TemplateExprEnd: return "TemplateExprEnd";
        case TokenType::TemplateEnd: return "TemplateEnd";

        case TokenType::Let: return "Let";
        case TokenType::Const: return "Const";
        case TokenType::Int: return "Int";
        case TokenType::Float: return "Float";
        case TokenType::Bool: return "Bool";
        case TokenType::Str: return "Str";
        case TokenType::Fn: return "Fn";
        case TokenType::Ref: return "Ref";
        case TokenType::Class: return "Class";
        case TokenType::Extends: return "Extends";
        case TokenType::Priv: return "Priv";
        case TokenType::Init: return "Init";
        case TokenType::This: return "This";
        case TokenType::If: return "If";
        case TokenType::Else: return "Else";
        case TokenType::Switch: return "Switch";
        case TokenType::Case: return "Case";
        case TokenType::Default: return "Default";
        case TokenType::Break: return "Break";
        case TokenType::Try: return "Try";
        case TokenType::Catch: return "Catch";
        case TokenType::While: return "While";
        case TokenType::For: return "For";
        case TokenType::In: return "In";
        case TokenType::Import: return "Import";
        case TokenType::From: return "From";
        case TokenType::Enum: return "Enum";
        case TokenType::Return: return "Return";
        case TokenType::Map: return "Map";

        case TokenType::Plus: return "Plus";
        case TokenType::Minus: return "Minus";
        case TokenType::Star: return "Star";
        case TokenType::Slash: return "Slash";
        case TokenType::Percent: return "Percent";
        case TokenType::StarStar: return "StarStar";
        case TokenType::PlusPlus: return "PlusPlus";
        case TokenType::MinusMinus: return "MinusMinus";

        case TokenType::EqualEqual: return "EqualEqual";
        case TokenType::BangEqual: return "BangEqual";
        case TokenType::Less: return "Less";
        case TokenType::Greater: return "Greater";
        case TokenType::LessEqual: return "LessEqual";
        case TokenType::GreaterEqual: return "GreaterEqual";

        case TokenType::AmpAmp: return "AmpAmp";
        case TokenType::PipePipe: return "PipePipe";
        case TokenType::Bang: return "Bang";

        case TokenType::Equal: return "Equal";
        case TokenType::PlusEqual: return "PlusEqual";
        case TokenType::MinusEqual: return "MinusEqual";
        case TokenType::StarEqual: return "StarEqual";
        case TokenType::SlashEqual: return "SlashEqual";

        case TokenType::FatArrow: return "FatArrow";

        case TokenType::LBrace: return "LBrace";
        case TokenType::RBrace: return "RBrace";
        case TokenType::LBracket: return "LBracket";
        case TokenType::RBracket: return "RBracket";
        case TokenType::LParen: return "LParen";
        case TokenType::RParen: return "RParen";
        case TokenType::Colon: return "Colon";
        case TokenType::Semicolon: return "Semicolon";
        case TokenType::Comma: return "Comma";
        case TokenType::Dot: return "Dot";
        case TokenType::Question: return "Question";

        case TokenType::Eof: return "Eof";
        case TokenType::Invalid: return "Invalid";
    }
    return "Unknown";
}

//keywords reserved by the language; note 'true'/'false'/'null' are literal
//keywords (they produce BoolLiteral/NullLiteral tokens), not plain identifiers
static const std::unordered_map<std::string, TokenType>& keywordTable(){
    static const std::unordered_map<std::string, TokenType> table = {
        {"let", TokenType::Let},
        {"const", TokenType::Const},
        {"int", TokenType::Int},
        {"float", TokenType::Float},
        {"bool", TokenType::Bool},
        {"str", TokenType::Str},
        {"null", TokenType::NullLiteral},
        {"true", TokenType::BoolLiteral},
        {"false", TokenType::BoolLiteral},
        {"fn", TokenType::Fn},
        {"ref", TokenType::Ref},
        {"class", TokenType::Class},
        {"extends", TokenType::Extends},
        {"priv", TokenType::Priv},
        {"init", TokenType::Init},
        {"this", TokenType::This},
        {"if", TokenType::If},
        {"else", TokenType::Else},
        {"switch", TokenType::Switch},
        {"case", TokenType::Case},
        {"default", TokenType::Default},
        {"break", TokenType::Break},
        {"try", TokenType::Try},
        {"catch", TokenType::Catch},
        {"while", TokenType::While},
        {"for", TokenType::For},
        {"in", TokenType::In},
        {"import", TokenType::Import},
        {"from", TokenType::From},
        {"enum", TokenType::Enum},
        {"return", TokenType::Return},
        {"Map", TokenType::Map},
    };
    return table;
}

const TokenType* lookupKeyword(const std::string& text){
    const auto& table = keywordTable();
    auto it = table.find(text);
    if(it == table.end()){
        return nullptr;
    }
    return &it->second;
}

}//namespace zinc
