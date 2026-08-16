#pragma once

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast/Ast.h"
#include "runtime/Environment.h"
#include "runtime/Value.h"

namespace zinc{

/**
 * a tree-walking interpreter over the checked AST. mirrors Checker's overall
 * shape (a declaration-collection pre-pass so forward references/recursion
 * work, then a single execution walk) but is otherwise independent of it -
 * runtime Values are not tied to the checker's Type representation.
 *
 * control flow (`return`, `break`) is implemented via small internal
 * exception types that unwind the C++ call stack to the nearest enclosing
 * function call / loop-or-switch, a standard technique for tree-walkers.
 *
 * known, deliberate limitation: `if`/`try` used as *expressions* have no
 * decided "what value does the block yield" semantics (see FIXME.md). This
 * interpreter yields `null` as an internal placeholder only - that is NOT
 * an established Zinc language rule and must not be relied upon.
 */
class Interpreter{
public:
    Interpreter();

    void run(const Program& program);

private:
    struct MethodInfo{
        std::vector<std::string> paramNames;
        std::vector<bool> paramIsRef;
        const std::vector<std::unique_ptr<Stmt>>* body = nullptr;
    };
    struct ClassInfo{
        std::string name;
        std::string baseClassName;
        std::vector<std::pair<std::string, const Expr*>> fieldInits; //this class's own fields, declared order
        std::unordered_map<std::string, MethodInfo> methods;
        bool hasCtor = false;
        MethodInfo ctor;
    };
    struct EnumInfo{
        std::set<std::string> variants;
    };

    //thrown to unwind to the nearest function call / loop-or-switch
    struct ReturnSignal{ Value value; };
    struct BreakSignal{};

    //RAII helper: temporarily swaps currentEnv_, restores it even if an
    //exception (ReturnSignal/BreakSignal/RuntimeException) unwinds through
    class ScopedEnv{
    public:
        ScopedEnv(Interpreter& interp, std::shared_ptr<Environment> newEnv)
            : interp_(interp), prev_(interp.currentEnv_){
            interp.currentEnv_ = std::move(newEnv);
        }
        ~ScopedEnv(){ interp_.currentEnv_ = prev_; }
        ScopedEnv(const ScopedEnv&) = delete;
    private:
        Interpreter& interp_;
        std::shared_ptr<Environment> prev_;
    };

    std::shared_ptr<Environment> globals_;
    std::shared_ptr<Environment> currentEnv_;
    std::unordered_map<std::string, ClassInfo> classes_;
    std::unordered_map<std::string, EnumInfo> enums_;
    std::unordered_map<std::string, Value> builtinRegistry_; //name -> builtin Value, populated once at construction

    void collectDeclarations(const Program& program);

    void execStmtList(const std::vector<std::unique_ptr<Stmt>>& stmts);
    void execStmt(Stmt* stmt);
    Value evalExpr(Expr* expr);

    Value callFunction(const FunctionValue& fn, std::vector<Value>& args, const Value* thisValue, size_t line, size_t column);
    Value callNative(const Value& fnVal, std::vector<Value>& args, size_t line, size_t column);
    Value instantiateClass(const std::string& className, std::vector<Arg>& argExprs, size_t line, size_t column);

    void evalLvalueSet(Expr* target, Value value);

    const ClassInfo* findClass(const std::string& name) const;
    const MethodInfo* findMethod(const std::string& className, const std::string& methodName, std::string& ownerClassOut) const;

    Value arithmeticOp(const Value& l, const Value& r, const std::string& op, size_t line, size_t column);
    Value comparisonOp(const Value& l, const Value& r, const std::string& op);
    static bool valuesEqual(const Value& a, const Value& b);

    [[noreturn]] void error(size_t line, size_t column, const std::string& message);
};

}//namespace zinc