#pragma once

#include <memory>
#include <string>
#include <vector>

namespace zinc{

struct Expr;
struct Stmt;

/** a parsed type: BaseType ['?'] ['[]'], where BaseType may itself be Map<K,V> */
struct TypeNode{
    std::string name;    //primitive keyword ("int"/"float"/"bool"/"str") or a class/identifier name; empty when isMap
    bool isMap = false;
    std::unique_ptr<TypeNode> mapKeyType;   //set when isMap
    std::unique_ptr<TypeNode> mapValueType; //set when isMap
    bool nullable = false;
    bool isArray = false;
};

/** fn/param declarator: [ref] Type name [[]] */
struct Param{
    bool isRef = false;
    std::unique_ptr<TypeNode> type;
    std::string name;
    bool isArray = false;
};

/** call-site argument: [ref] Expr */
struct Arg{
    bool isRef = false;
    std::unique_ptr<Expr> value;
};

/** one piece of a template string: either raw text, or an interpolated expr */
struct TemplatePart{
    bool isExpr = false;
    std::string text;             //set when !isExpr
    std::unique_ptr<Expr> expr;   //set when isExpr
};

/** { "key": value } entry */
struct MapEntryNode{
    std::string key;
    std::unique_ptr<Expr> value;
};

enum class ExprKind{
    IntLiteral, FloatLiteral, StringLiteral, BoolLiteral, NullLiteral,
    Identifier, This,
    TemplateString,
    ArrayLiteral, MapLiteral,
    Lambda,
    If, Try,
    Call, Index, Property,
    Unary, Update, Binary, Assignment
};

/**
 * a single expression node. only the fields relevant to `kind` are populated;
 * see the comment above each group for which kind(s) use it.
 */
struct Expr{
    ExprKind kind;
    size_t line = 0, column = 0;

    //IntLiteral/FloatLiteral: raw digit text. StringLiteral: decoded text.
    //Identifier: the name. Update/Unary/Binary/Assignment: the operator text.
    std::string text;

    //BoolLiteral
    bool boolValue = false;

    //TemplateString
    std::vector<TemplatePart> templateParts;

    //ArrayLiteral
    std::vector<std::unique_ptr<Expr>> elements;

    //MapLiteral
    std::vector<MapEntryNode> mapEntries;

    //Lambda: params by name only (untyped, per grammar). body is either an
    //expr (bodyExpr set) or a block (isBlockBody true, block populated)
    std::vector<std::string> lambdaParams;
    bool isBlockBody = false;
    std::unique_ptr<Expr> bodyExpr;
    std::vector<std::unique_ptr<Stmt>> block;

    //If: condition + block above, optional else. elseIsIf picks which of
    //elseIf/elseBlock is populated
    std::unique_ptr<Expr> condition;
    bool hasElse = false;
    bool elseIsIf = false;
    std::unique_ptr<Expr> elseIf;
    std::vector<std::unique_ptr<Stmt>> elseBlock;

    //Try: block above is the try body. catch clause below
    bool catchHasType = false;
    std::unique_ptr<TypeNode> catchType;
    std::string catchName;
    std::vector<std::unique_ptr<Stmt>> catchBlock;

    //Call: target = callee, args = call arguments
    //Index: target = indexed expr, right = index expr
    //Property: target = object, text = property name
    //Unary: target = operand, text = op ("!" | "-" | "++" | "--" for prefix)
    //Update: target = operand, text = "++"|"--", isPrefix distinguishes ++x from x++
    //Binary: target = left, right = right, text = op
    //Assignment: target = lvalue, right = value, text = op
    std::unique_ptr<Expr> target;
    std::unique_ptr<Expr> right;
    std::vector<Arg> args;
    bool isPrefix = true;
};

enum class StmtKind{
    Import, EnumDecl, ClassDecl, FnDecl, VarDecl,
    ExprStmt, //covers ExprStmt ';', and IfExpr/TryExpr used in statement position
    Switch, While, ForIn, ForC, Return, Break
};

struct CaseClauseNode{
    std::unique_ptr<Expr> value;
    std::vector<std::unique_ptr<Stmt>> body;
};

/** a class body member: a var decl, a method, or the constructor */
struct ClassMemberNode{
    bool isPriv = false;

    enum class Kind{ Var, Fn, Ctor } kind;

    //Var: 'let'/'const' (type is null => inferred) or an explicit Type
    bool isConst = false;
    std::unique_ptr<TypeNode> type; //null => inferred via let/const
    bool isArray = false;
    std::string name;
    std::unique_ptr<Expr> initExpr;

    //Fn / Ctor
    std::vector<Param> params;
    std::vector<std::unique_ptr<Stmt>> body;
};

/**
 * a single statement node. as with Expr, only the fields relevant to `kind`
 * are populated.
 */
struct Stmt{
    StmtKind kind;
    size_t line = 0, column = 0;

    //Import
    bool isNamedImport = false;
    std::vector<std::string> importNames; //set when isNamedImport
    std::string importPath;

    //EnumDecl: name + variants. ClassDecl: name + baseClassName + members.
    //FnDecl: name + params + body. VarDecl: name + type info below.
    std::string name;
    std::vector<std::string> enumVariants;
    std::string baseClassName; //ClassDecl, empty if no 'extends'
    std::vector<ClassMemberNode> members;
    std::vector<Param> params;
    std::vector<std::unique_ptr<Stmt>> body;

    //VarDecl (also reused for the init clause of a C-style for loop)
    bool isConst = false;
    std::unique_ptr<TypeNode> type; //null => inferred via let/const
    bool isArray = false;
    std::unique_ptr<Expr> initExpr;

    //ExprStmt
    std::unique_ptr<Expr> expr;

    //Switch
    std::unique_ptr<Expr> subject;
    std::vector<CaseClauseNode> cases;
    bool hasDefault = false;
    std::vector<std::unique_ptr<Stmt>> defaultBody;

    //While / ForIn / ForC share bodyBlock as the loop body
    std::vector<std::unique_ptr<Stmt>> bodyBlock;
    std::unique_ptr<Expr> condition; //While condition, or ForC condition

    //ForIn
    std::string loopVarName;
    std::unique_ptr<Expr> iterable;

    //ForC
    std::unique_ptr<Stmt> forInit; //a VarDecl Stmt
    std::unique_ptr<Expr> forIncrement;

    //Return
    std::unique_ptr<Expr> returnValue; //null => bare 'return'
};

using Program = std::vector<std::unique_ptr<Stmt>>;

}//namespace zinc
