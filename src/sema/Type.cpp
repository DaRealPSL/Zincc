#include "sema/Type.h"

namespace zinc{

Type typeFromAstNode(const TypeNode* node){
    if(node == nullptr){
        return Type::makeUnknown();
    }

    Type result;
    if(node->isMap){
        result = Type::makeMap(typeFromAstNode(node->mapKeyType.get()), typeFromAstNode(node->mapValueType.get()));
    }else if(node->name == "int"){
        result = Type::makeInt();
    }else if(node->name == "float"){
        result = Type::makeFloat();
    }else if(node->name == "bool"){
        result = Type::makeBool();
    }else if(node->name == "str"){
        result = Type::makeStr();
    }else{
        //a bare identifier type: might be a class or an enum. default to
        //Class here; the checker corrects this to Enum once it knows which
        //declarations exist (see Checker::resolveNamedType)
        result = Type::makeClass(node->name);
    }

    if(node->isArray){
        result = Type::makeArray(result);
    }
    if(node->nullable){
        result.nullable = true;
    }
    return result;
}

bool sameBaseType(const Type& a, const Type& b){
    if(a.kind != b.kind) return false;
    switch(a.kind){
        case TypeKind::Class:
        case TypeKind::Enum:
            return a.name == b.name;
        case TypeKind::Array:
            return sameBaseType(*a.elementType, *b.elementType);
        case TypeKind::Map:
            return sameBaseType(*a.mapKeyType, *b.mapKeyType) && sameBaseType(*a.mapValueType, *b.mapValueType);
        default:
            return true;
    }
}

std::string typeToString(const Type& type){
    std::string base;
    switch(type.kind){
        case TypeKind::Int: base = "int"; break;
        case TypeKind::Float: base = "float"; break;
        case TypeKind::Bool: base = "bool"; break;
        case TypeKind::Str: base = "str"; break;
        case TypeKind::Null: base = "null"; break;
        case TypeKind::Void: base = "void"; break;
        case TypeKind::Class: base = type.name; break;
        case TypeKind::Enum: base = type.name; break;
        case TypeKind::Array: base = typeToString(*type.elementType) + "[]"; break;
        case TypeKind::Map: base = "Map<" + typeToString(*type.mapKeyType) + ", " + typeToString(*type.mapValueType) + ">"; break;
        case TypeKind::Unknown: base = "<unknown>"; break;
    }
    return type.nullable ? base + "?" : base;
}

}//namespace zinc
