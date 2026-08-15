#pragma once

#include <memory>
#include <string>

#include "ast/Ast.h"

namespace zinc{

enum class TypeKind{
    Int, Float, Bool, Str,
    Null,    //the type of the literal `null` before it's matched against a T?
    Void,    //a function/lambda that never returns a value
    Class,
    Enum,
    Array,
    Map,
    Unknown  //inference failed, or the construct's typing isn't decided yet;
             //deliberately permissive so one unknown doesn't cascade into a
             //wall of unrelated errors

    //NOTE (see FIXME.md #4, heterogeneous maps): if/when a decision is made
    //on Any vs. a union type for map literals with mixed value types, it
    //belongs here as a new TypeKind, with matching cases added to
    //isAssignable/unify/sameBaseType/typeToString. Not added yet since the
    //mechanism hasn't been decided - deliberately not guessing.
};

struct Type{
    TypeKind kind = TypeKind::Unknown;
    bool nullable = false;
    std::string name; //Class/Enum name
    std::shared_ptr<Type> elementType; //Array
    std::shared_ptr<Type> mapKeyType;  //Map
    std::shared_ptr<Type> mapValueType; //Map

    static Type makeInt(){ Type t; t.kind = TypeKind::Int; return t; }
    static Type makeFloat(){ Type t; t.kind = TypeKind::Float; return t; }
    static Type makeBool(){ Type t; t.kind = TypeKind::Bool; return t; }
    static Type makeStr(){ Type t; t.kind = TypeKind::Str; return t; }
    static Type makeNull(){ Type t; t.kind = TypeKind::Null; return t; }
    static Type makeVoid(){ Type t; t.kind = TypeKind::Void; return t; }
    static Type makeUnknown(){ Type t; t.kind = TypeKind::Unknown; return t; }

    static Type makeClass(std::string className){
        Type t;
        t.kind = TypeKind::Class;
        t.name = std::move(className);
        return t;
    }
    static Type makeEnum(std::string enumName){
        Type t;
        t.kind = TypeKind::Enum;
        t.name = std::move(enumName);
        return t;
    }
    static Type makeArray(Type element){
        Type t;
        t.kind = TypeKind::Array;
        t.elementType = std::make_shared<Type>(std::move(element));
        return t;
    }
    static Type makeMap(Type key, Type value){
        Type t;
        t.kind = TypeKind::Map;
        t.mapKeyType = std::make_shared<Type>(std::move(key));
        t.mapValueType = std::make_shared<Type>(std::move(value));
        return t;
    }

    bool isNumeric() const{ return kind == TypeKind::Int || kind == TypeKind::Float; }
    bool isUnknown() const{ return kind == TypeKind::Unknown; }

    /** returns a copy with nullable forced false, used after a narrowing null-check */
    Type asNonNull() const{
        Type t = *this;
        t.nullable = false;
        return t;
    }

    /** returns a copy with nullable forced true */
    Type asNullable() const{
        Type t = *this;
        t.nullable = true;
        return t;
    }
};

/** converts a parsed TypeNode (from the AST) into a checker Type; className resolution
 *  against declared classes/enums happens in the checker, not here */
Type typeFromAstNode(const TypeNode* node);

/** structural equality, ignoring nullability */
bool sameBaseType(const Type& a, const Type& b);

std::string typeToString(const Type& type);

}//namespace zinc
