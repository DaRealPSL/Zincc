#include "runtime/Builtins.h"

#include <cmath>
#include <iostream>

#include "runtime/RuntimeError.h"

namespace zinc{

namespace{

double asDouble(const Value& v){
    return v.kind == ValueKind::Float ? v.floatValue : static_cast<double>(v.intValue);
}

void requireArity(const std::vector<Value>& args, size_t n, const std::string& fnName, size_t line, size_t column){
    if(args.size() != n){
        throw RuntimeException{RuntimeError{
            fnName + "() expects " + std::to_string(n) + " argument(s), got " + std::to_string(args.size()),
            line, column
        }};
    }
}

Value nativePrint(Interpreter&, std::vector<Value>& args, size_t line, size_t column){
    requireArity(args, 1, "print", line, column);
    std::cout << stringifyValue(args[0]) << "\n";
    return Value::makeNull();
}

Value nativeMathFloor(Interpreter&, std::vector<Value>& args, size_t line, size_t column){
    requireArity(args, 1, "math.floor", line, column);
    return Value::makeInt(static_cast<long long>(std::floor(asDouble(args[0]))));
}

Value nativeMathSqrt(Interpreter&, std::vector<Value>& args, size_t line, size_t column){
    requireArity(args, 1, "math.sqrt", line, column);
    return Value::makeFloat(std::sqrt(asDouble(args[0])));
}

Value nativeMathAbs(Interpreter&, std::vector<Value>& args, size_t line, size_t column){
    requireArity(args, 1, "math.abs", line, column);
    if(args[0].kind == ValueKind::Float) return Value::makeFloat(std::fabs(args[0].floatValue));
    return Value::makeInt(std::llabs(args[0].intValue));
}

}//namespace

void registerBuiltins(std::unordered_map<std::string, Value>& registry){
    registry["print"] = Value::makeNativeFunction(nativePrint);

    auto mathNs = std::make_shared<NamespaceValue>();
    mathNs->name = "math";
    mathNs->members["floor"] = Value::makeNativeFunction(nativeMathFloor);
    mathNs->members["sqrt"] = Value::makeNativeFunction(nativeMathSqrt);
    mathNs->members["abs"] = Value::makeNativeFunction(nativeMathAbs);

    Value mathVal;
    mathVal.kind = ValueKind::Namespace;
    mathVal.namespaceValue = mathNs;
    registry["math"] = mathVal;
}

}//namespace zinc