#include "runtime/Value.h"

#include <sstream>

namespace zinc{

bool Value::isTruthy() const{
    if(kind == ValueKind::Bool) return boolValue;
    if(kind == ValueKind::Null) return false;
    return true;
}

std::string stringifyValue(const Value& value){
    switch(value.kind){
        case ValueKind::Null: return "null";
        case ValueKind::Int: return std::to_string(value.intValue);
        case ValueKind::Float:{
            std::ostringstream out;
            out << value.floatValue;
            return out.str();
        }
        case ValueKind::Bool: return value.boolValue ? "true" : "false";
        case ValueKind::Str: return value.strValue;
        case ValueKind::Array:{
            std::string out = "[";
            for(size_t i = 0; i < value.arrayValue->size(); i++){
                if(i > 0) out += ", ";
                out += stringifyValue((*value.arrayValue)[i]);
            }
            out += "]";
            return out;
        }
        case ValueKind::Map:{
            std::string out = "{";
            bool first = true;
            for(const auto& [key, val] : *value.mapValue){
                if(!first) out += ", ";
                first = false;
                out += "\"" + key + "\": " + stringifyValue(val);
            }
            out += "}";
            return out;
        }
        case ValueKind::Function:
            return "<fn " + (value.functionValue ? value.functionValue->name : std::string("anonymous")) + ">";
        case ValueKind::NativeFunction:
            return "<native fn>";
        case ValueKind::Instance:
            return "<" + (value.instanceValue ? value.instanceValue->className : std::string("?")) + " instance>";
        case ValueKind::Namespace:
            return "<namespace " + (value.namespaceValue ? value.namespaceValue->name : std::string("?")) + ">";
        case ValueKind::EnumVariant:
            return value.enumName + "." + value.enumVariant;
    }
    return "<?>";
}

}//namespace zinc