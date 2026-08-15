#include "ast/AstPrinter.h"

#include <sstream>

namespace zinc{

namespace{

void indent(std::ostringstream& out, int depth){
    for(int i = 0; i < depth; i++){
        out << "  ";
    }
}

void printType(const TypeNode& type, std::ostringstream& out){
    if(type.isMap){
        out << "Map<";
        printType(*type.mapKeyType, out);
        out << ", ";
        printType(*type.mapValueType, out);
        out << ">";
    }else{
        out << type.name;
    }
    if(type.nullable) out << "?";
    if(type.isArray) out << "[]";
}

std::string typeToString(const TypeNode* type){
    if(type == nullptr) return "<inferred>";
    std::ostringstream out;
    printType(*type, out);
    return out.str();
}

void printStmtList(const std::vector<std::unique_ptr<Stmt>>& stmts, std::ostringstream& out, int depth);
void printExpr(const Expr* expr, std::ostringstream& out, int depth);

void printParams(const std::vector<Param>& params, std::ostringstream& out){
    out << "(";
    for(size_t i = 0; i < params.size(); i++){
        if(i > 0) out << ", ";
        if(params[i].isRef) out << "ref ";
        out << typeToString(params[i].type.get()) << " " << params[i].name;
        if(params[i].isArray) out << "[]";
    }
    out << ")";
}

void printExpr(const Expr* expr, std::ostringstream& out, int depth){
    if(expr == nullptr){
        indent(out, depth);
        out << "<null>\n";
        return;
    }
    indent(out, depth);
    switch(expr->kind){
        case ExprKind::IntLiteral:
            out << "IntLiteral " << expr->text << "\n";
            break;
        case ExprKind::FloatLiteral:
            out << "FloatLiteral " << expr->text << "\n";
            break;
        case ExprKind::StringLiteral:
            out << "StringLiteral \"" << expr->text << "\"\n";
            break;
        case ExprKind::BoolLiteral:
            out << "BoolLiteral " << (expr->boolValue ? "true" : "false") << "\n";
            break;
        case ExprKind::NullLiteral:
            out << "NullLiteral\n";
            break;
        case ExprKind::Identifier:
            out << "Identifier " << expr->text << "\n";
            break;
        case ExprKind::This:
            out << "This\n";
            break;
        case ExprKind::TemplateString:
            out << "TemplateString\n";
            for(const TemplatePart& part : expr->templateParts){
                if(part.isExpr){
                    indent(out, depth + 1);
                    out << "Interp:\n";
                    printExpr(part.expr.get(), out, depth + 2);
                }else{
                    indent(out, depth + 1);
                    out << "Text \"" << part.text << "\"\n";
                }
            }
            break;
        case ExprKind::ArrayLiteral:
            out << "ArrayLiteral\n";
            for(const auto& el : expr->elements){
                printExpr(el.get(), out, depth + 1);
            }
            break;
        case ExprKind::MapLiteral:
            out << "MapLiteral\n";
            for(const MapEntryNode& entry : expr->mapEntries){
                indent(out, depth + 1);
                out << entry.key << ":\n";
                printExpr(entry.value.get(), out, depth + 2);
            }
            break;
        case ExprKind::Lambda:
            out << "Lambda(";
            for(size_t i = 0; i < expr->lambdaParams.size(); i++){
                if(i > 0) out << ", ";
                out << expr->lambdaParams[i];
            }
            out << ")\n";
            if(expr->isBlockBody){
                printStmtList(expr->block, out, depth + 1);
            }else{
                printExpr(expr->bodyExpr.get(), out, depth + 1);
            }
            break;
        case ExprKind::If:
            out << "If\n";
            indent(out, depth + 1);
            out << "Condition:\n";
            printExpr(expr->condition.get(), out, depth + 2);
            indent(out, depth + 1);
            out << "Then:\n";
            printStmtList(expr->block, out, depth + 2);
            if(expr->hasElse){
                indent(out, depth + 1);
                out << "Else:\n";
                if(expr->elseIsIf){
                    printExpr(expr->elseIf.get(), out, depth + 2);
                }else{
                    printStmtList(expr->elseBlock, out, depth + 2);
                }
            }
            break;
        case ExprKind::Try:
            out << "Try\n";
            indent(out, depth + 1);
            out << "Block:\n";
            printStmtList(expr->block, out, depth + 2);
            indent(out, depth + 1);
            out << "Catch(" << (expr->catchHasType ? typeToString(expr->catchType.get()) + " " : "")
                << expr->catchName << "):\n";
            printStmtList(expr->catchBlock, out, depth + 2);
            break;
        case ExprKind::Call:
            out << "Call\n";
            indent(out, depth + 1);
            out << "Target:\n";
            printExpr(expr->target.get(), out, depth + 2);
            indent(out, depth + 1);
            out << "Args:\n";
            for(const Arg& arg : expr->args){
                indent(out, depth + 2);
                out << (arg.isRef ? "ref\n" : "\n");
                printExpr(arg.value.get(), out, depth + 3);
            }
            break;
        case ExprKind::Index:
            out << "Index\n";
            printExpr(expr->target.get(), out, depth + 1);
            printExpr(expr->right.get(), out, depth + 1);
            break;
        case ExprKind::Property:
            out << "Property ." << expr->text << "\n";
            printExpr(expr->target.get(), out, depth + 1);
            break;
        case ExprKind::Unary:
            out << "Unary " << expr->text << "\n";
            printExpr(expr->target.get(), out, depth + 1);
            break;
        case ExprKind::Update:
            out << "Update " << expr->text << " (" << (expr->isPrefix ? "prefix" : "postfix") << ")\n";
            printExpr(expr->target.get(), out, depth + 1);
            break;
        case ExprKind::Binary:
            out << "Binary " << expr->text << "\n";
            printExpr(expr->target.get(), out, depth + 1);
            printExpr(expr->right.get(), out, depth + 1);
            break;
        case ExprKind::Assignment:
            out << "Assignment " << expr->text << "\n";
            printExpr(expr->target.get(), out, depth + 1);
            printExpr(expr->right.get(), out, depth + 1);
            break;
    }
}

void printClassMember(const ClassMemberNode& member, std::ostringstream& out, int depth){
    indent(out, depth);
    out << (member.isPriv ? "priv " : "");
    switch(member.kind){
        case ClassMemberNode::Kind::Var:
            out << "Field " << typeToString(member.type.get()) << (member.isArray ? "[]" : "")
                << " " << member.name << "\n";
            printExpr(member.initExpr.get(), out, depth + 1);
            break;
        case ClassMemberNode::Kind::Fn:
            out << "Method " << member.name;
            printParams(member.params, out);
            out << "\n";
            printStmtList(member.body, out, depth + 1);
            break;
        case ClassMemberNode::Kind::Ctor:
            out << "Constructor";
            printParams(member.params, out);
            out << "\n";
            printStmtList(member.body, out, depth + 1);
            break;
    }
}

void printStmt(const Stmt* stmt, std::ostringstream& out, int depth){
    if(stmt == nullptr){
        indent(out, depth);
        out << "<null>\n";
        return;
    }
    indent(out, depth);
    switch(stmt->kind){
        case StmtKind::Import:
            if(stmt->isNamedImport){
                out << "Import {";
                for(size_t i = 0; i < stmt->importNames.size(); i++){
                    if(i > 0) out << ", ";
                    out << stmt->importNames[i];
                }
                out << "} from \"" << stmt->importPath << "\"\n";
            }else{
                out << "Import \"" << stmt->importPath << "\"\n";
            }
            break;
        case StmtKind::EnumDecl:
            out << "Enum " << stmt->name << " {";
            for(size_t i = 0; i < stmt->enumVariants.size(); i++){
                if(i > 0) out << ", ";
                out << stmt->enumVariants[i];
            }
            out << "}\n";
            break;
        case StmtKind::ClassDecl:
            out << "Class " << stmt->name;
            if(!stmt->baseClassName.empty()) out << " extends " << stmt->baseClassName;
            out << "\n";
            for(const ClassMemberNode& member : stmt->members){
                printClassMember(member, out, depth + 1);
            }
            break;
        case StmtKind::FnDecl:
            out << "Fn " << stmt->name;
            printParams(stmt->params, out);
            out << "\n";
            printStmtList(stmt->body, out, depth + 1);
            break;
        case StmtKind::VarDecl:
            out << (stmt->type ? typeToString(stmt->type.get()) : (stmt->isConst ? "const" : "let"))
                << (stmt->isArray ? "[]" : "") << " " << stmt->name << " =\n";
            printExpr(stmt->initExpr.get(), out, depth + 1);
            break;
        case StmtKind::ExprStmt:
            out << "ExprStmt\n";
            printExpr(stmt->expr.get(), out, depth + 1);
            break;
        case StmtKind::Switch:
            out << "Switch\n";
            printExpr(stmt->subject.get(), out, depth + 1);
            for(const CaseClauseNode& c : stmt->cases){
                indent(out, depth + 1);
                out << "Case:\n";
                printExpr(c.value.get(), out, depth + 2);
                printStmtList(c.body, out, depth + 2);
            }
            if(stmt->hasDefault){
                indent(out, depth + 1);
                out << "Default:\n";
                printStmtList(stmt->defaultBody, out, depth + 2);
            }
            break;
        case StmtKind::While:
            out << "While\n";
            printExpr(stmt->condition.get(), out, depth + 1);
            printStmtList(stmt->bodyBlock, out, depth + 1);
            break;
        case StmtKind::ForIn:
            out << "ForIn " << stmt->loopVarName << "\n";
            printExpr(stmt->iterable.get(), out, depth + 1);
            printStmtList(stmt->bodyBlock, out, depth + 1);
            break;
        case StmtKind::ForC:
            out << "ForC\n";
            printStmt(stmt->forInit.get(), out, depth + 1);
            printExpr(stmt->condition.get(), out, depth + 1);
            printExpr(stmt->forIncrement.get(), out, depth + 1);
            printStmtList(stmt->bodyBlock, out, depth + 1);
            break;
        case StmtKind::Return:
            out << "Return\n";
            if(stmt->returnValue){
                printExpr(stmt->returnValue.get(), out, depth + 1);
            }
            break;
        case StmtKind::Break:
            out << "Break\n";
            break;
    }
}

void printStmtList(const std::vector<std::unique_ptr<Stmt>>& stmts, std::ostringstream& out, int depth){
    for(const auto& s : stmts){
        printStmt(s.get(), out, depth);
    }
}

}//namespace

std::string printProgram(const Program& program){
    std::ostringstream out;
    for(const auto& stmt : program){
        printStmt(stmt.get(), out, 0);
    }
    return out.str();
}

}//namespace zinc
