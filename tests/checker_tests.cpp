#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "sema/Checker.h"

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

std::vector<SemanticError> checkSource(const std::string& source){
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    Program program = parser.parseProgram();
    if(!lexer.errors().empty() || !parser.errors().empty()){
        std::cout << "[FAIL] (fixture broken - lex/parse errors in: " << source << ")\n";
        failures++;
        return {};
    }
    Checker checker;
    checker.check(program);
    return checker.errors();
}

}//namespace

void testIntFloatCoercion(){
    check("int + float -> float, no error", checkSource("let x = 1 + 2.5;").empty());
    check("int assignable to float var", checkSource("float y = 1;").empty());
    check("float not assignable to int var", !checkSource("int z = 1.5;").empty());
}

void testArithmeticRequiresNumeric(){
    check("bool + int is a type error", !checkSource("let x = true + 1;").empty());
}

void testStringConcatenation(){
    check("str + str concatenates with no error",
        checkSource("let x = \"a\" + \"b\";").empty());
    check("str + int is still a type error",
        !checkSource("let x = \"a\" + 1;").empty());
    check("int + str is still a type error",
        !checkSource("let x = 1 + \"a\";").empty());
    check("concatenation result usable as str",
        checkSource("str x = \"a\" + \"b\";").empty());
    check("concatenating a possibly-null str requires a null check",
        !checkSource("fn test(str? s){ let x = s + \"!\"; }").empty());
    check("narrowed nullable str concatenates fine",
        checkSource("fn test(str? s){ if s != null { let x = s + \"!\"; } }").empty());
}

void testNullSafetyEnforcement(){
    check("nullable arg to non-nullable param errors",
        !checkSource("fn takeStr(str s){ } fn test(str? s){ takeStr(s); }").empty());
    check("if x != null narrows for the then-block",
        checkSource("fn takeStr(str s){ } fn test(str? s){ if s != null { takeStr(s); } }").empty());
    check("if x == null { return; } narrows the rest of the block",
        checkSource("fn takeStr(str s){ } fn test(str? s){ if s == null { return; } takeStr(s); }").empty());
}

void testFunctionReturnTypeInference(){
    check("unified int/float return assignable to float",
        checkSource("fn pick(bool b){ if b{ return 1; } return 2.5; } fn test(){ float y = pick(true); }").empty());
    check("unified int/float return NOT assignable to int",
        !checkSource("fn pick(bool b){ if b{ return 1; } return 2.5; } fn test(){ int y = pick(true); }").empty());
    check("incompatible return types across branches error",
        !checkSource("fn bad(bool b){ if b{ return 1; } return true; }").empty());
}

void testClassFieldsMethodsInheritance(){
    check("inherited field and method resolve with no error",
        checkSource(
            "class Animal{ str name = \"\"; fn speak(){ } }"
            "class Dog extends Animal{ fn bark(){ } }"
            "fn test(){ Dog d = Dog(); d.bark(); d.speak(); let n = d.name; }"
        ).empty());
    check("calling an undeclared method errors",
        !checkSource("class Animal{ } fn test(){ Animal a = Animal(); a.bark(); }").empty());
}

void testUndefinedIdentifier(){
    check("using an undeclared name errors", !checkSource("let x = y + 1;").empty());
}

void testBreakOutsideLoop(){
    check("bare break at top level errors", !checkSource("break;").empty());
    check("break inside a loop is fine", checkSource("fn test(){ while true{ break; } }").empty());
}

void testRefParams(){
    check("matching ref on both sides is fine",
        checkSource("fn inc(ref int x){ x += 1; } fn test(){ int n = 1; inc(ref n); }").empty());
    check("missing ref at call site errors",
        !checkSource("fn inc(ref int x){ } fn test(){ int n = 1; inc(n); }").empty());
    check("extra ref where param isn't ref errors",
        !checkSource("fn add(int x){ } fn test(){ int n = 1; add(ref n); }").empty());
}

void testConstReassignment(){
    check("assigning to a const variable errors",
        !checkSource("fn test(){ const x = 1; x = 2; }").empty());
}

void testArrayAndMapChecks(){
    check("consistent array literal element types are fine",
        checkSource("int arr[] = [1, 2, 3];").empty());
    check("inconsistent array literal element types error",
        !checkSource("int arr[] = [1, true];").empty());
    check("map literal and lookup are fine",
        checkSource("let m = {\"a\": 1, \"b\": 2}; int v = m[\"a\"];").empty());
    check("indexing an int array with a string errors",
        !checkSource("int arr[] = [1, 2]; int bad = arr[\"x\"];").empty());
}

void testSwitchCaseTypeMismatch(){
    check("case type matching subject is fine",
        checkSource("fn test(){ int x = 1; switch x{ case 1: break; default: break; } }").empty());
    check("case type not matching subject errors",
        !checkSource("fn test(){ int x = 1; switch x{ case 1: break; case true: break; } }").empty());
}

void testEnumVariantAccess(){
    check("valid enum variant access is fine",
        checkSource("enum Rarity{ Common, Rare } fn test(){ let r = Rarity.Common; }").empty());
    check("unknown enum variant errors",
        !checkSource("enum Rarity{ Common } fn test(){ let r = Rarity.Nope; }").empty());
}

int main(){
    testIntFloatCoercion();
    testArithmeticRequiresNumeric();
    testStringConcatenation();
    testNullSafetyEnforcement();
    testFunctionReturnTypeInference();
    testClassFieldsMethodsInheritance();
    testUndefinedIdentifier();
    testBreakOutsideLoop();
    testRefParams();
    testConstReassignment();
    testArrayAndMapChecks();
    testSwitchCaseTypeMismatch();
    testEnumVariantAccess();

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
