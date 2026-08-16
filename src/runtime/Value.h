#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast/Ast.h"

namespace zinc{

class Environment;
class Interpreter;
struct Instance;
struct NamespaceValue;
struct FunctionValue;
struct Value;

enum class ValueKind{
    Null, Int, Float, Bool, Str,
    Array, Map,
    Function,       //user-defined fn/lambda closure
    NativeFunction, //builtin (print, math.floor, ...)
    Instance,       //a class instance
    Namespace,      //a stdlib namespace value, e.g. `math`
    EnumVariant      //e.g. Rarity.Common
};

using NativeFn = std::function<Value(Interpreter&, std::vector<Value>&, size_t line, size_t column)>;

/**
 * a runtime value. deliberately the same "fat struct + kind tag" style as
 * the AST/Type representations, for consistency with the rest of the
 * codebase - only the fields relevant to `kind` are populated.
 *
 * Int/Float/Bool are plain fields (value semantics). Str is a plain
 * std::string (also value semantics - Zinc has no string mutation syntax).
 * Array/Map/Instance/Function/Namespace are shared_ptr-held (reference
 * semantics), which is what makes `arr[0] = x` and instance field mutation
 * behave the way the checker's isLvalue already assumes.
 */
struct Value{
    ValueKind kind = ValueKind::Null;

    long long intValue = 0;
    double floatValue = 0.0;
    bool boolValue = false;
    std::string strValue;

    std::shared_ptr<std::vector<Value>> arrayValue;
    std::shared_ptr<std::unordered_map<std::string, Value>> mapValue;
    std::shared_ptr<FunctionValue> functionValue;
    std::shared_ptr<Instance> instanceValue;
    std::shared_ptr<NamespaceValue> namespaceValue;
    NativeFn nativeFn;

    //EnumVariant
    std::string enumName;
    std::string enumVariant;

    static Value makeNull(){ return Value{}; }
    static Value makeInt(long long v){ Value r; r.kind = ValueKind::Int; r.intValue = v; return r; }
    static Value makeFloat(double v){ Value r; r.kind = ValueKind::Float; r.floatValue = v; return r; }
    static Value makeBool(bool v){ Value r; r.kind = ValueKind::Bool; r.boolValue = v; return r; }
    static Value makeStr(std::string v){ Value r; r.kind = ValueKind::Str; r.strValue = std::move(v); return r; }
    static Value makeArray(std::vector<Value> elems){
        Value r; r.kind = ValueKind::Array;
        r.arrayValue = std::make_shared<std::vector<Value>>(std::move(elems));
        return r;
    }
    static Value makeMap(std::unordered_map<std::string, Value> entries){
        Value r; r.kind = ValueKind::Map;
        r.mapValue = std::make_shared<std::unordered_map<std::string, Value>>(std::move(entries));
        return r;
    }
    static Value makeEnumVariant(std::string enumName_, std::string variant){
        Value r; r.kind = ValueKind::EnumVariant;
        r.enumName = std::move(enumName_);
        r.enumVariant = std::move(variant);
        return r;
    }
    static Value makeNativeFunction(NativeFn fn){
        Value r; r.kind = ValueKind::NativeFunction;
        r.nativeFn = std::move(fn);
        return r;
    }

    bool isTruthy() const;
};

/** a user-defined function or lambda, plus the environment it closed over */
struct FunctionValue{
    std::string name; //"<lambda>" if anonymous, for diagnostics
    std::vector<std::string> paramNames;
    std::vector<bool> paramIsRef;
    bool isBlockBody = true;
    const std::vector<std::unique_ptr<Stmt>>* bodyBlock = nullptr; //set when isBlockBody
    const Expr* bodyExpr = nullptr;                                //set when !isBlockBody
    std::shared_ptr<Environment> closureEnv;
    std::string ownerClass; //non-empty for methods/ctors, so `this` can be bound
};

struct Instance{
    std::string className;
    std::unordered_map<std::string, Value> fields;
};

struct NamespaceValue{
    std::string name;
    std::unordered_map<std::string, Value> members;
};

/** renders a value the way print()/template interpolation should display it */
std::string stringifyValue(const Value& value);

}//namespace zinc