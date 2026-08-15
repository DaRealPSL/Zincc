#include "ast/AstPrinter.h"
#include "lexer/Lexer.h"
#include "lexer/Token.h"
#include "parser/Parser.h"
#include "sema/Checker.h"

#include <fstream>
#include <iostream>
#include <sstream>

/** reads a whole file into a string; sets ok=false if it couldn't be opened */
static std::string readFile(const std::string& path, bool& ok){
    std::ifstream file(path);
    if(!file){
        ok = false;
        return "";
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    ok = true;
    return buffer.str();
}

int main(int argc, char** argv){
    if(argc < 2){
        std::cerr << "usage: zincc <source-file>\n";
        return 1;
    }

    bool ok = false;
    std::string source = readFile(argv[1], ok);
    if(!ok){
        std::cerr << "error: could not open file '" << argv[1] << "'\n";
        return 1;
    }

    zinc::Lexer lexer(source);
    std::vector<zinc::Token> tokens = lexer.tokenize();

    if(!lexer.errors().empty()){
        std::cerr << lexer.errors().size() << " lex error(s):\n";
        for(const zinc::LexError& err : lexer.errors()){
            std::cerr << "  " << err.line << ":" << err.column << "  " << err.message << "\n";
        }
        return 1;
    }

    zinc::Parser parser(std::move(tokens));
    zinc::Program program = parser.parseProgram();

    if(!parser.errors().empty()){
        std::cerr << parser.errors().size() << " parse error(s):\n";
        for(const zinc::ParseError& err : parser.errors()){
            std::cerr << "  " << err.line << ":" << err.column << "  " << err.message << "\n";
        }
        return 1;
    }

    zinc::Checker checker;
    checker.check(program);

    if(!checker.errors().empty()){
        std::cerr << checker.errors().size() << " semantic error(s):\n";
        for(const zinc::SemanticError& err : checker.errors()){
            std::cerr << "  " << err.line << ":" << err.column << "  " << err.message << "\n";
        }
        return 1;
    }

    std::cout << zinc::printProgram(program);
    return 0;
}

