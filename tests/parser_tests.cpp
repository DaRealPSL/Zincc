#include "ast/Ast.h"
#include "ast/AstPrinter.h"
#include "lexer/Lexer.h"
#include "parser/Parser.h"

#include <iostream>
#include <string>
#include <vector>

using namespace zinc;

namespace{

int failures = 0;

void check(const std::string& name, bool condition){
    if(condition){
        std::cout << "[PASS] " << name << "\n";
    }else{
        std::cout << "[FAIL] " << name << "\n";
        failures++;
    }
}

Program parse(const std::string& source, std::vector<ParseError>* errorsOut = nullptr){
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    Program program = parser.parseProgram();
    if(errorsOut){
        *errorsOut = parser.errors();
    }
    return program;
}

}//namespace

void testVarDeclLetConstTyped(){
    auto program = parse("let a = 1; const b = 2; int c = 3; str? d = null; int e[] = [1, 2];");
    check("var decl count", program.size() == 5);
    check("let infers no type", program[0]->kind == StmtKind::VarDecl && !program[0]->type && !program[0]->isConst);
    check("const infers no type", program[1]->kind == StmtKind::VarDecl && !program[1]->type && program[1]->isConst);
    check("explicit type decl", program[2]->kind == StmtKind::VarDecl && program[2]->type && program[2]->type->name == "int");
    check("nullable type decl", program[3]->kind == StmtKind::VarDecl && program[3]->type && program[3]->type->nullable);
    check("array bracket after identifier", program[4]->kind == StmtKind::VarDecl && program[4]->isArray);
}

void testFnDeclAndRefParams(){
    auto program = parse("fn add(int x, ref int y){ return x + y; }");
    check("fn decl count", program.size() == 1);
    const auto& fn = program[0];
    check("fn kind", fn->kind == StmtKind::FnDecl);
    check("fn name", fn->name == "add");
    check("param count", fn->params.size() == 2);
    check("first param not ref", !fn->params[0].isRef);
    check("second param is ref", fn->params[1].isRef);
    check("second param type", fn->params[1].type->name == "int");
    check("body has one return statement", fn->body.size() == 1 && fn->body[0]->kind == StmtKind::Return);
}

void testClassDefaultPublicPriv(){
    auto program = parse(
        "class Bubble extends Item{"
        "  priv str name = \"\";"
        "  int value = 0;"
        "  fn init(str n){ this.name = n; }"
        "  fn describe(){ return this.name; }"
        "}"
    );
    check("class decl count", program.size() == 1);
    const auto& cls = program[0];
    check("class kind", cls->kind == StmtKind::ClassDecl);
    check("class name", cls->name == "Bubble");
    check("extends clause", cls->baseClassName == "Item");
    check("member count", cls->members.size() == 4);
    check("explicit priv field", cls->members[0].isPriv);
    check("field public by default", !cls->members[1].isPriv);
    check("constructor recognized", cls->members[2].kind == ClassMemberNode::Kind::Ctor);
    check("method recognized", cls->members[3].kind == ClassMemberNode::Kind::Fn && cls->members[3].name == "describe");
}

void testIfAsExpressionAndStatement(){
    //note: these blocks intentionally use ordinary statements (assignments),
    //not a bare trailing expr - "does a block yield a value" hasn't been
    //decided, so this only exercises If parsing as syntax, not semantics
    auto program = parse(
        "let x = if a > 1{ b = 1; }else{ b = 2; };"
        "if a > 1{ b = 1; }else if a > 2{ b = 2; }else{ b = 3; }"
    );
    check("stmt count", program.size() == 2);
    check("if used as an expression", program[0]->kind == StmtKind::VarDecl && program[0]->initExpr->kind == ExprKind::If);
    check("if used as a statement", program[1]->kind == StmtKind::ExprStmt && program[1]->expr->kind == ExprKind::If);
    check("else-if chains", program[1]->expr->hasElse && program[1]->expr->elseIsIf);
}

void testTryCatchWithAndWithoutType(){
    auto program = parse(
        "try{ risky(); }catch(err){ log(err); }"
        "try{ risky(); }catch(NetworkError err){ log(err); }"
    );
    check("stmt count", program.size() == 2);
    check("catch without type", !program[0]->expr->catchHasType && program[0]->expr->catchName == "err");
    check("catch with type", program[1]->expr->catchHasType && program[1]->expr->catchType->name == "NetworkError");
}

void testSwitchStatement(){
    auto program = parse(
        "switch x{"
        "case 1: y = 1; break;"
        "case 2: y = 2; break;"
        "default: y = 0;"
        "}"
    );
    check("stmt count", program.size() == 1);
    check("switch kind", program[0]->kind == StmtKind::Switch);
    check("case count", program[0]->cases.size() == 2);
    check("has default", program[0]->hasDefault);
}

void testWhileForInForC(){
    auto program = parse(
        "while x < 10{ x += 1; }"
        "for n in items{ total += n; }"
        "for let i = 0; i < 10; i += 1{ total += i; }"
    );
    check("stmt count", program.size() == 3);
    check("while kind", program[0]->kind == StmtKind::While);
    check("for-in kind and var name", program[1]->kind == StmtKind::ForIn && program[1]->loopVarName == "n");
    check("for-c kind has init/cond/incr", program[2]->kind == StmtKind::ForC
        && program[2]->forInit != nullptr && program[2]->condition != nullptr && program[2]->forIncrement != nullptr);
}

void testLambdaAndRefCallSite(){
    auto program = parse(
        "let add = (a, b) => a + b;"
        "changeValue(ref total);"
    );
    check("stmt count", program.size() == 2);
    const auto& lambda = program[0]->initExpr;
    check("lambda kind", lambda->kind == ExprKind::Lambda);
    check("lambda param count", lambda->lambdaParams.size() == 2);
    check("lambda expr body", !lambda->isBlockBody && lambda->bodyExpr->kind == ExprKind::Binary);
    check("ref at call site", program[1]->expr->kind == ExprKind::Call
        && program[1]->expr->args.size() == 1 && program[1]->expr->args[0].isRef);
}

void testOperatorPrecedence(){
    auto program = parse("let r = 1 + 2 * 3;");
    const auto& top = program[0]->initExpr;
    check("top op is +", top->kind == ExprKind::Binary && top->text == "+");
    check("left is 1", top->target->kind == ExprKind::IntLiteral && top->target->text == "1");
    check("right is 2 * 3", top->right->kind == ExprKind::Binary && top->right->text == "*");
}

void testTemplateStringParsing(){
    auto program = parse("let s = `hi {name}!`;");
    const auto& tmpl = program[0]->initExpr;
    check("template kind", tmpl->kind == ExprKind::TemplateString);
    check("template part count", tmpl->templateParts.size() == 3);
    check("first part text", !tmpl->templateParts[0].isExpr && tmpl->templateParts[0].text == "hi ");
    check("second part is identifier expr", tmpl->templateParts[1].isExpr
        && tmpl->templateParts[1].expr->kind == ExprKind::Identifier);
    check("third part text", !tmpl->templateParts[2].isExpr && tmpl->templateParts[2].text == "!");
}

void testArrayAndMapLiterals(){
    auto program = parse("let arr = [1, 2, 3]; let m = {\"a\": 1, \"b\": 2};");
    check("array element count", program[0]->initExpr->elements.size() == 3);
    check("map entry count", program[1]->initExpr->mapEntries.size() == 2);
    check("map first key", program[1]->initExpr->mapEntries[0].key == "a");
}

void testErrorRecoveryReportsAndContinues(){
    std::vector<ParseError> errors;
    auto program = parse("let = 5; let y = 10;", &errors);
    check("one error reported", errors.size() == 1);
    check("parser recovered and parsed the next statement", program.size() == 1 && program[0]->name == "y");
}

void testPrinterDoesNotCrashOnRealisticProgram(){
    auto program = parse(
        "enum Rarity{ Common, Rare }"
        "class Bubble{"
        "  priv int value;"
        "  fn init(int value){ this.value = value; }"
        "}"
        "fn sumAll(int nums[]){"
        "  let total = 0;"
        "  for n in nums{ total += n; }"
        "  return total;"
        "}"
    );
    std::string printed = printProgram(program);
    check("printer produced non-empty output", !printed.empty());
}

int main(){
    testVarDeclLetConstTyped();
    testFnDeclAndRefParams();
    testClassDefaultPublicPriv();
    testIfAsExpressionAndStatement();
    testTryCatchWithAndWithoutType();
    testSwitchStatement();
    testWhileForInForC();
    testLambdaAndRefCallSite();
    testOperatorPrecedence();
    testTemplateStringParsing();
    testArrayAndMapLiterals();
    testErrorRecoveryReportsAndContinues();
    testPrinterDoesNotCrashOnRealisticProgram();

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
