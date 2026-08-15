#pragma once

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast/Ast.h"
#include "sema/Type.h"

namespace zinc{

struct SemanticError{
    std::string message;
    size_t line, column;
};

/** a resolved function/method/constructor signature */
struct FunctionSig{
    std::vector<std::string> paramNames;
    std::vector<Type> paramTypes;
    std::vector<bool> paramIsRef;
    Type returnType = Type::makeUnknown();
    bool returnTypeResolved = false;
    bool isResolving = false; //cycle guard for recursive/mutually-recursive calls
    bool isCtor = false;
    std::string ownerClass; //empty for free functions
    const std::vector<std::unique_ptr<Stmt>>* body = nullptr;
    size_t line = 0, column = 0;
};

struct ClassInfo{
    std::string name;
    std::string baseClassName; //empty if no 'extends'
    std::unordered_map<std::string, Type> fields;
};

struct EnumInfo{
    std::set<std::string> variants;
};

/**
 * two-pass semantic analyzer:
 *  1. collectDeclarations walks the whole program twice: once to register
 *     every class/enum/function *name*, then again to fill in field/param
 *     types (now that all names are known, so forward references and
 *     self-referential types work).
 *  2. check walks every statement, inferring/verifying types. function and
 *     method bodies are checked lazily (on first call, or when their own
 *     declaration is reached, whichever comes first) so call order doesn't
 *     matter; a return-type cycle (recursion) resolves to Unknown rather
 *     than erroring, which is a deliberate simplification - see the notes
 *     in Checker.cpp.
 *
 * known, deliberate limitations (not silently invented, called out here):
 *  - null-safety narrowing only recognizes `if x != null {}` and
 *    `if x == null { return/break; }`, not arbitrary flow analysis.
 *  - calling a class name (`Bubble(...)`) is treated as constructing an
 *    instance, since the grammar never defined constructor-call syntax.
 *  - `if`/`try` used as expressions type as Unknown, since block "value"
 *    semantics were never decided.
 *  - lambdas have no modeled function type (params are Unknown-typed, per
 *    grammar, and a lambda's own type is Unknown); its body is still checked.
 */
class Checker{
public:
    void check(const Program& program);
    const std::vector<SemanticError>& errors() const{ return errors_; }

private:
    struct VarInfo{
        Type type;
        bool isConst = false;
    };
    struct Scope{
        std::unordered_map<std::string, VarInfo> vars;
        std::set<std::string> narrowedNonNull;
    };

    std::unordered_map<std::string, FunctionSig> functions_; //free fns by name, methods by "Class::name"
    std::unordered_map<std::string, ClassInfo> classes_;
    std::unordered_map<std::string, EnumInfo> enums_;
    std::unordered_map<std::string, Type> importedGlobals_;
    std::vector<Scope> scopes_;
    std::string currentClassName_;
    std::vector<Type>* currentReturnCollector_ = nullptr;
    int loopDepth_ = 0;
    int switchDepth_ = 0;
    std::vector<SemanticError> errors_;

    void error(size_t line, size_t column, const std::string& message);

    //-- declaration collection --
    void collectDeclarations(const Program& program);
    void collectFunctionSig(const std::string& key, const std::vector<Param>& params,
                             const std::vector<std::unique_ptr<Stmt>>& body,
                             const std::string& ownerClass, bool isCtor, size_t line, size_t column);

    //-- scopes / vars --
    void pushScope();
    void popScope();
    void declareVar(const std::string& name, const Type& type, bool isConst, size_t line, size_t column);
    void narrowNonNull(const std::string& name);
    Type resolveIdentifierType(const std::string& name, size_t line, size_t column);

    //-- type helpers --
    Type resolveNamedType(Type type, size_t line, size_t column);
    Type unify(const Type& a, const Type& b, bool& ok);
    bool isAssignable(const Type& target, const Type& value);
    bool classIsSubtypeOf(const std::string& derived, const std::string& base);
    const ClassInfo* findClass(const std::string& name) const;
    const Type* findFieldType(const std::string& className, const std::string& fieldName) const;
    FunctionSig* findMethodSig(const std::string& className, const std::string& methodName, std::string& ownerClassOut);

    //-- flow helpers --
    static bool isLvalue(const Expr* expr);
    static bool blockAlwaysExits(const std::vector<std::unique_ptr<Stmt>>& block);
    static std::string extractNullComparedVar(const Expr* cond, const std::string& op);

    //-- checking --
    void ensureFunctionChecked(const std::string& key);
    void checkStmtList(const std::vector<std::unique_ptr<Stmt>>& stmts);
    void checkStmt(Stmt* stmt);
    Type checkExpr(Expr* expr);
    void checkCallArgs(FunctionSig& sig, std::vector<Arg>& args, const std::string& calleeName, size_t line, size_t column);
};

}//namespace zinc
