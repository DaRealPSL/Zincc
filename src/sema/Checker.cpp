#include "sema/Checker.h"

#include <algorithm>

namespace zinc{

void Checker::error(size_t line, size_t column, const std::string& message){
    errors_.push_back(SemanticError{message, line, column});
}

//== scopes / vars ============================================================

void Checker::pushScope(){
    scopes_.push_back(Scope{});
}

void Checker::popScope(){
    scopes_.pop_back();
}

void Checker::declareVar(const std::string& name, const Type& type, bool isConst, size_t line, size_t column){
    if(scopes_.back().vars.count(name)){
        error(line, column, "'" + name + "' is already declared in this scope");
    }
    scopes_.back().vars[name] = VarInfo{type, isConst};
}

void Checker::narrowNonNull(const std::string& name){
    scopes_.back().narrowedNonNull.insert(name);
}

Type Checker::resolveIdentifierType(const std::string& name, size_t line, size_t column){
    for(auto it = scopes_.rbegin(); it != scopes_.rend(); ++it){
        auto vit = it->vars.find(name);
        if(vit != it->vars.end()){
            Type t = vit->second.type;
            for(const Scope& s : scopes_){
                if(s.narrowedNonNull.count(name)){
                    t.nullable = false;
                    break;
                }
            }
            return t;
        }
    }
    auto impIt = importedGlobals_.find(name);
    if(impIt != importedGlobals_.end()){
        return impIt->second;
    }
    error(line, column, "undefined identifier '" + name + "'");
    return Type::makeUnknown();
}

//== type helpers =============================================================

Type Checker::resolveNamedType(Type type, size_t line, size_t column){
    if(type.kind == TypeKind::Class){
        if(enums_.count(type.name)){
            Type r = Type::makeEnum(type.name);
            r.nullable = type.nullable;
            return r;
        }
        if(!classes_.count(type.name)){
            error(line, column, "unknown type '" + type.name + "'");
        }
        return type;
    }
    if(type.kind == TypeKind::Array){
        Type r = type;
        r.elementType = std::make_shared<Type>(resolveNamedType(*type.elementType, line, column));
        return r;
    }
    if(type.kind == TypeKind::Map){
        Type r = type;
        r.mapKeyType = std::make_shared<Type>(resolveNamedType(*type.mapKeyType, line, column));
        r.mapValueType = std::make_shared<Type>(resolveNamedType(*type.mapValueType, line, column));
        return r;
    }
    return type;
}

Type Checker::unify(const Type& a, const Type& b, bool& ok){
    ok = true;
    if(a.isUnknown()) return b;
    if(b.isUnknown()) return a;
    if(a.kind == TypeKind::Null && b.kind == TypeKind::Null) return a;
    if(a.kind == TypeKind::Null){ Type r = b; r.nullable = true; return r; }
    if(b.kind == TypeKind::Null){ Type r = a; r.nullable = true; return r; }
    if(a.isNumeric() && b.isNumeric()){
        Type r = (a.kind == TypeKind::Float || b.kind == TypeKind::Float) ? Type::makeFloat() : Type::makeInt();
        r.nullable = a.nullable || b.nullable;
        return r;
    }
    if(a.kind == TypeKind::Class && b.kind == TypeKind::Class){
        if(classIsSubtypeOf(a.name, b.name)){ Type r = b; r.nullable = a.nullable || b.nullable; return r; }
        if(classIsSubtypeOf(b.name, a.name)){ Type r = a; r.nullable = a.nullable || b.nullable; return r; }
        ok = false;
        return Type::makeUnknown();
    }
    if(sameBaseType(a, b)){ Type r = a; r.nullable = a.nullable || b.nullable; return r; }
    ok = false;
    return Type::makeUnknown();
}

bool Checker::isAssignable(const Type& target, const Type& value){
    if(target.isUnknown() || value.isUnknown()) return true;
    if(value.kind == TypeKind::Null) return target.nullable;
    if(value.nullable && !target.nullable) return false;
    if(target.isNumeric() && value.isNumeric()){
        if(target.kind == TypeKind::Float) return true; //int or float widens to float
        return value.kind == TypeKind::Int;              //int target needs an int value
    }
    if(target.kind == TypeKind::Class && value.kind == TypeKind::Class){
        return classIsSubtypeOf(value.name, target.name);
    }
    if(target.kind == TypeKind::Array && value.kind == TypeKind::Array){
        return value.elementType->isUnknown() || isAssignable(*target.elementType, *value.elementType);
    }
    if(target.kind == TypeKind::Map && value.kind == TypeKind::Map){
        return sameBaseType(*target.mapKeyType, *value.mapKeyType)
            && (value.mapValueType->isUnknown() || isAssignable(*target.mapValueType, *value.mapValueType));
    }
    return sameBaseType(target, value);
}

bool Checker::classIsSubtypeOf(const std::string& derived, const std::string& base){
    if(derived == base) return true;
    const ClassInfo* info = findClass(derived);
    while(info && !info->baseClassName.empty()){
        if(info->baseClassName == base) return true;
        info = findClass(info->baseClassName);
    }
    return false;
}

const ClassInfo* Checker::findClass(const std::string& name) const{
    auto it = classes_.find(name);
    return it == classes_.end() ? nullptr : &it->second;
}

const Type* Checker::findFieldType(const std::string& className, const std::string& fieldName) const{
    const ClassInfo* info = findClass(className);
    while(info){
        auto it = info->fields.find(fieldName);
        if(it != info->fields.end()) return &it->second;
        if(info->baseClassName.empty()) break;
        info = findClass(info->baseClassName);
    }
    return nullptr;
}

FunctionSig* Checker::findMethodSig(const std::string& className, const std::string& methodName, std::string& ownerClassOut){
    std::string cur = className;
    while(!cur.empty()){
        const ClassInfo* info = findClass(cur);
        if(!info) break;
        std::string key = cur + "::" + methodName;
        auto it = functions_.find(key);
        if(it != functions_.end()){
            ownerClassOut = cur;
            return &it->second;
        }
        cur = info->baseClassName;
    }
    return nullptr;
}

//== flow helpers =============================================================

bool Checker::isLvalue(const Expr* expr){
    return expr->kind == ExprKind::Identifier || expr->kind == ExprKind::Property || expr->kind == ExprKind::Index;
}

bool Checker::blockAlwaysExits(const std::vector<std::unique_ptr<Stmt>>& block){
    return !block.empty() && (block.back()->kind == StmtKind::Return || block.back()->kind == StmtKind::Break);
}

std::string Checker::extractNullComparedVar(const Expr* cond, const std::string& op){
    if(cond->kind != ExprKind::Binary || cond->text != op) return "";
    if(cond->target->kind == ExprKind::Identifier && cond->right->kind == ExprKind::NullLiteral) return cond->target->text;
    if(cond->right->kind == ExprKind::Identifier && cond->target->kind == ExprKind::NullLiteral) return cond->right->text;
    return "";
}

//== declaration collection ===================================================

void Checker::collectFunctionSig(const std::string& key, const std::vector<Param>& params,
                                  const std::vector<std::unique_ptr<Stmt>>& body,
                                  const std::string& ownerClass, bool isCtor, size_t line, size_t column){
    FunctionSig sig;
    sig.ownerClass = ownerClass;
    sig.isCtor = isCtor;
    sig.body = &body;
    sig.line = line;
    sig.column = column;
    for(const Param& p : params){
        Type t = resolveNamedType(typeFromAstNode(p.type.get()), line, column);
        if(p.isArray) t = Type::makeArray(t);
        sig.paramNames.push_back(p.name);
        sig.paramTypes.push_back(t);
        sig.paramIsRef.push_back(p.isRef);
    }
    functions_[key] = std::move(sig);
}

void Checker::collectDeclarations(const Program& program){
    //pass A: register every class/enum name so field/param types below can
    //reference any class or enum regardless of declaration order
    for(const auto& stmt : program){
        if(stmt->kind == StmtKind::ClassDecl){
            ClassInfo info;
            info.name = stmt->name;
            info.baseClassName = stmt->baseClassName;
            classes_[stmt->name] = std::move(info);
        }else if(stmt->kind == StmtKind::EnumDecl){
            EnumInfo info;
            for(const std::string& v : stmt->enumVariants) info.variants.insert(v);
            enums_[stmt->name] = std::move(info);
        }
    }

    //pass B: now fill in function/class member signatures
    for(const auto& stmt : program){
        if(stmt->kind == StmtKind::FnDecl){
            collectFunctionSig(stmt->name, stmt->params, stmt->body, "", false, stmt->line, stmt->column);
        }else if(stmt->kind == StmtKind::ClassDecl){
            ClassInfo& info = classes_[stmt->name];
            for(const ClassMemberNode& member : stmt->members){
                if(member.kind == ClassMemberNode::Kind::Var){
                    //let/const fields (member.type == nullptr) get their real type
                    //filled in later, on first full check of the class body
                    Type t = member.type ? resolveNamedType(typeFromAstNode(member.type.get()), stmt->line, stmt->column)
                                          : Type::makeUnknown();
                    if(member.isArray) t = Type::makeArray(t);
                    info.fields[member.name] = t;
                }else if(member.kind == ClassMemberNode::Kind::Fn){
                    collectFunctionSig(stmt->name + "::" + member.name, member.params, member.body,
                                        stmt->name, false, stmt->line, stmt->column);
                }else{ //Ctor
                    collectFunctionSig(stmt->name + "::init", member.params, member.body,
                                        stmt->name, true, stmt->line, stmt->column);
                }
            }
        }
    }
}

//== function body checking ===================================================

void Checker::ensureFunctionChecked(const std::string& key){
    auto it = functions_.find(key);
    if(it == functions_.end()) return;
    FunctionSig& sig = it->second;
    if(sig.returnTypeResolved || sig.isResolving) return;
    sig.isResolving = true;

    pushScope();
    std::string prevClass = currentClassName_;
    currentClassName_ = sig.ownerClass;
    if(!sig.ownerClass.empty()){
        declareVar("this", Type::makeClass(sig.ownerClass), true, sig.line, sig.column);
    }
    for(size_t i = 0; i < sig.paramNames.size(); i++){
        declareVar(sig.paramNames[i], sig.paramTypes[i], false, sig.line, sig.column);
    }

    std::vector<Type> returnTypes;
    std::vector<Type>* prevCollector = currentReturnCollector_;
    currentReturnCollector_ = &returnTypes;
    checkStmtList(*sig.body);
    currentReturnCollector_ = prevCollector;
    currentClassName_ = prevClass;
    popScope();

    if(sig.isCtor){
        sig.returnType = Type::makeVoid();
    }else if(returnTypes.empty()){
        sig.returnType = Type::makeVoid();
    }else{
        Type acc = returnTypes[0];
        for(size_t i = 1; i < returnTypes.size(); i++){
            bool ok = false;
            Type unified = unify(acc, returnTypes[i], ok);
            if(!ok){
                error(sig.line, sig.column, "function '" + key + "' returns incompatible types ("
                      + typeToString(acc) + " vs " + typeToString(returnTypes[i]) + ")");
                acc = Type::makeUnknown();
                break;
            }
            acc = unified;
        }
        sig.returnType = acc;
    }
    sig.returnTypeResolved = true;
    sig.isResolving = false;
}

//== statements ================================================================

void Checker::checkStmtList(const std::vector<std::unique_ptr<Stmt>>& stmts){
    for(const auto& s : stmts){
        checkStmt(s.get());

        //early-return null-guard: `if x == null { return/break; }` with no
        //else narrows x to non-null for the rest of *this* block
        if(s->kind == StmtKind::ExprStmt && s->expr && s->expr->kind == ExprKind::If && !s->expr->hasElse){
            if(blockAlwaysExits(s->expr->block)){
                std::string narrowed = extractNullComparedVar(s->expr->condition.get(), "==");
                if(!narrowed.empty()){
                    narrowNonNull(narrowed);
                }
            }
        }
    }
}

void Checker::checkStmt(Stmt* stmt){
    switch(stmt->kind){
        case StmtKind::Import:
            if(stmt->isNamedImport){
                for(const std::string& name : stmt->importNames){
                    importedGlobals_[name] = Type::makeUnknown();
                }
            }
            break;

        case StmtKind::EnumDecl:
            break; //already collected

        case StmtKind::ClassDecl:
            for(ClassMemberNode& member : stmt->members){
                if(member.kind == ClassMemberNode::Kind::Var){
                    pushScope();
                    currentClassName_ = stmt->name;
                    declareVar("this", Type::makeClass(stmt->name), true, stmt->line, stmt->column);
                    Type initType = checkExpr(member.initExpr.get());
                    currentClassName_.clear();
                    popScope();

                    Type& declared = classes_[stmt->name].fields[member.name];
                    if(member.type == nullptr){
                        //let/const field: infer from the initializer
                        declared = initType;
                        if(member.isArray && declared.kind != TypeKind::Array){
                            error(stmt->line, stmt->column, "field '" + member.name + "' declared as an array but initialized with a non-array value");
                        }
                    }else if(!isAssignable(declared, initType)){
                        error(stmt->line, stmt->column, "field '" + member.name + "': cannot assign "
                              + typeToString(initType) + " to " + typeToString(declared));
                    }
                }else{
                    std::string key = stmt->name + "::" + (member.kind == ClassMemberNode::Kind::Ctor ? "init" : member.name);
                    ensureFunctionChecked(key);
                }
            }
            break;

        case StmtKind::FnDecl:
            ensureFunctionChecked(stmt->name);
            break;

        case StmtKind::VarDecl:{
            Type initType = checkExpr(stmt->initExpr.get());
            Type finalType;
            if(stmt->type){
                finalType = resolveNamedType(typeFromAstNode(stmt->type.get()), stmt->line, stmt->column);
                if(stmt->isArray) finalType = Type::makeArray(finalType);
                if(!isAssignable(finalType, initType)){
                    error(stmt->line, stmt->column, "cannot assign " + typeToString(initType) + " to " + typeToString(finalType));
                }
            }else{
                finalType = initType;
                if(stmt->isArray && finalType.kind != TypeKind::Array){
                    error(stmt->line, stmt->column, "'" + stmt->name + "' declared as an array but initialized with a non-array value");
                }
            }
            declareVar(stmt->name, finalType, stmt->isConst, stmt->line, stmt->column);
            break;
        }

        case StmtKind::ExprStmt:
            checkExpr(stmt->expr.get());
            break;

        case StmtKind::Switch:{
            Type subjectType = checkExpr(stmt->subject.get());
            switchDepth_++;
            for(CaseClauseNode& c : stmt->cases){
                Type caseType = checkExpr(c.value.get());
                if(!subjectType.isUnknown() && !caseType.isUnknown() && !sameBaseType(subjectType, caseType)){
                    error(stmt->line, stmt->column, "case value type " + typeToString(caseType)
                          + " does not match switch subject type " + typeToString(subjectType));
                }
                pushScope();
                checkStmtList(c.body);
                popScope();
            }
            if(stmt->hasDefault){
                pushScope();
                checkStmtList(stmt->defaultBody);
                popScope();
            }
            switchDepth_--;
            break;
        }

        case StmtKind::While:{
            Type condType = checkExpr(stmt->condition.get());
            if(!condType.isUnknown() && condType.kind != TypeKind::Bool){
                error(stmt->line, stmt->column, "while condition must be bool, got " + typeToString(condType));
            }
            std::string narrowed = extractNullComparedVar(stmt->condition.get(), "!=");
            loopDepth_++;
            pushScope();
            if(!narrowed.empty()) narrowNonNull(narrowed);
            checkStmtList(stmt->bodyBlock);
            popScope();
            loopDepth_--;
            break;
        }

        case StmtKind::ForIn:{
            Type iterableType = checkExpr(stmt->iterable.get());
            Type elemType = Type::makeUnknown();
            if(iterableType.kind == TypeKind::Array){
                elemType = *iterableType.elementType;
            }else if(!iterableType.isUnknown()){
                error(stmt->line, stmt->column, "for-in requires an array, got " + typeToString(iterableType));
            }
            loopDepth_++;
            pushScope();
            declareVar(stmt->loopVarName, elemType, false, stmt->line, stmt->column);
            checkStmtList(stmt->bodyBlock);
            popScope();
            loopDepth_--;
            break;
        }

        case StmtKind::ForC:{
            pushScope();
            checkStmt(stmt->forInit.get());
            Type condType = checkExpr(stmt->condition.get());
            if(!condType.isUnknown() && condType.kind != TypeKind::Bool){
                error(stmt->line, stmt->column, "for condition must be bool, got " + typeToString(condType));
            }
            checkExpr(stmt->forIncrement.get());
            loopDepth_++;
            pushScope();
            checkStmtList(stmt->bodyBlock);
            popScope();
            loopDepth_--;
            popScope();
            break;
        }

        case StmtKind::Return:
            if(currentReturnCollector_ == nullptr){
                error(stmt->line, stmt->column, "'return' used outside of a function");
                break;
            }
            if(stmt->returnValue){
                currentReturnCollector_->push_back(checkExpr(stmt->returnValue.get()));
            }else{
                currentReturnCollector_->push_back(Type::makeVoid());
            }
            break;

        case StmtKind::Break:
            if(loopDepth_ == 0 && switchDepth_ == 0){
                error(stmt->line, stmt->column, "'break' used outside of a loop or switch");
            }
            break;
    }
}

//== expressions ===============================================================

void Checker::checkCallArgs(FunctionSig& sig, std::vector<Arg>& args, const std::string& calleeName, size_t line, size_t column){
    if(args.size() != sig.paramNames.size()){
        error(line, column, "'" + calleeName + "' expects " + std::to_string(sig.paramNames.size())
              + " argument(s), got " + std::to_string(args.size()));
    }
    size_t n = std::min(args.size(), sig.paramNames.size());
    for(size_t i = 0; i < n; i++){
        Type argType = checkExpr(args[i].value.get());
        if(args[i].isRef != sig.paramIsRef[i]){
            error(line, column, "parameter " + std::to_string(i + 1) + " of '" + calleeName + "' "
                  + (sig.paramIsRef[i] ? "must be passed with 'ref'" : "is not a ref parameter"));
        }
        if(args[i].isRef && !isLvalue(args[i].value.get())){
            error(line, column, "ref argument " + std::to_string(i + 1) + " to '" + calleeName + "' must be a variable");
        }
        if(!isAssignable(sig.paramTypes[i], argType)){
            error(line, column, "argument " + std::to_string(i + 1) + " to '" + calleeName + "': expected "
                  + typeToString(sig.paramTypes[i]) + ", got " + typeToString(argType));
        }
    }
    for(size_t i = n; i < args.size(); i++){
        checkExpr(args[i].value.get());
    }
}

Type Checker::checkExpr(Expr* expr){
    switch(expr->kind){
        case ExprKind::IntLiteral: return Type::makeInt();
        case ExprKind::FloatLiteral: return Type::makeFloat();
        case ExprKind::StringLiteral: return Type::makeStr();
        case ExprKind::BoolLiteral: return Type::makeBool();
        case ExprKind::NullLiteral: return Type::makeNull();

        case ExprKind::This:
            if(currentClassName_.empty()){
                error(expr->line, expr->column, "'this' used outside of a class method");
                return Type::makeUnknown();
            }
            return Type::makeClass(currentClassName_);

        case ExprKind::Identifier:
            return resolveIdentifierType(expr->text, expr->line, expr->column);

        case ExprKind::TemplateString:
            for(TemplatePart& part : expr->templateParts){
                if(part.isExpr) checkExpr(part.expr.get());
            }
            return Type::makeStr();

        case ExprKind::ArrayLiteral:{
            if(expr->elements.empty()) return Type::makeArray(Type::makeUnknown());
            Type elem = checkExpr(expr->elements[0].get());
            for(size_t i = 1; i < expr->elements.size(); i++){
                Type next = checkExpr(expr->elements[i].get());
                bool ok = false;
                Type unified = unify(elem, next, ok);
                if(!ok){
                    error(expr->line, expr->column, "array elements have incompatible types: "
                          + typeToString(elem) + " and " + typeToString(next));
                }else{
                    elem = unified;
                }
            }
            return Type::makeArray(elem);
        }

        case ExprKind::MapLiteral:{
            //NOTE (see FIXME.md #4): map literals are currently required to
            //be homogeneous - this loop unifies every value's type and
            //errors on the first incompatible one. Whether Zinc eventually
            //wants heterogeneous maps (via Any, a union type, or staying
            //homogeneous) hasn't been decided; that decision isn't made
            //here. If/when it is, this is the one place that needs to
            //change - it's intentionally isolated from the rest of the
            //checker (isAssignable/unify aren't otherwise involved in this
            //specific homogeneity rule).
            if(expr->mapEntries.empty()) return Type::makeMap(Type::makeStr(), Type::makeUnknown());
            Type val = checkExpr(expr->mapEntries[0].value.get());
            for(size_t i = 1; i < expr->mapEntries.size(); i++){
                Type next = checkExpr(expr->mapEntries[i].value.get());
                bool ok = false;
                Type unified = unify(val, next, ok);
                if(!ok){
                    error(expr->line, expr->column, "map values have incompatible types");
                }else{
                    val = unified;
                }
            }
            return Type::makeMap(Type::makeStr(), val);
        }

        case ExprKind::Lambda:{
            pushScope();
            for(const std::string& p : expr->lambdaParams){
                declareVar(p, Type::makeUnknown(), false, expr->line, expr->column);
            }
            std::vector<Type> collected;
            std::vector<Type>* prevCollector = currentReturnCollector_;
            currentReturnCollector_ = &collected;
            if(expr->isBlockBody){
                checkStmtList(expr->block);
            }else{
                checkExpr(expr->bodyExpr.get());
            }
            currentReturnCollector_ = prevCollector;
            popScope();
            //lambdas have no modeled function type; the body above is still
            //fully checked, we just can't type the lambda value itself
            return Type::makeUnknown();
        }

        case ExprKind::If:{
            Type condType = checkExpr(expr->condition.get());
            if(!condType.isUnknown() && condType.kind != TypeKind::Bool){
                error(expr->line, expr->column, "if condition must be bool, got " + typeToString(condType));
            }
            std::string thenNarrow = extractNullComparedVar(expr->condition.get(), "!=");
            pushScope();
            if(!thenNarrow.empty()) narrowNonNull(thenNarrow);
            checkStmtList(expr->block);
            popScope();

            if(expr->hasElse){
                std::string elseNarrow = extractNullComparedVar(expr->condition.get(), "==");
                pushScope();
                if(!elseNarrow.empty()) narrowNonNull(elseNarrow);
                if(expr->elseIsIf){
                    checkExpr(expr->elseIf.get());
                }else{
                    checkStmtList(expr->elseBlock);
                }
                popScope();
            }
            //block "value" semantics were never decided, so If-as-a-value types as Unknown
            return Type::makeUnknown();
        }

        case ExprKind::Try:{
            pushScope();
            checkStmtList(expr->block);
            popScope();

            pushScope();
            Type catchType = Type::makeUnknown();
            if(expr->catchHasType){
                catchType = resolveNamedType(typeFromAstNode(expr->catchType.get()), expr->line, expr->column);
            }
            declareVar(expr->catchName, catchType, false, expr->line, expr->column);
            checkStmtList(expr->catchBlock);
            popScope();
            return Type::makeUnknown();
        }

        case ExprKind::Call:{
            if(expr->target->kind == ExprKind::Property){
                Expr* propNode = expr->target.get();
                Type objType = checkExpr(propNode->target.get());
                if(objType.isUnknown()){
                    for(Arg& a : expr->args) checkExpr(a.value.get());
                    return Type::makeUnknown();
                }
                if(objType.kind != TypeKind::Class){
                    error(expr->line, expr->column, "cannot call method '" + propNode->text
                          + "' on non-class type " + typeToString(objType));
                    for(Arg& a : expr->args) checkExpr(a.value.get());
                    return Type::makeUnknown();
                }
                if(objType.nullable){
                    error(expr->line, expr->column, "value may be null; check for null before calling '" + propNode->text + "'");
                }
                std::string ownerClass;
                FunctionSig* sig = findMethodSig(objType.name, propNode->text, ownerClass);
                if(!sig){
                    error(expr->line, expr->column, "class '" + objType.name + "' has no method '" + propNode->text + "'");
                    for(Arg& a : expr->args) checkExpr(a.value.get());
                    return Type::makeUnknown();
                }
                checkCallArgs(*sig, expr->args, propNode->text, expr->line, expr->column);
                ensureFunctionChecked(ownerClass + "::" + propNode->text);
                return functions_.at(ownerClass + "::" + propNode->text).returnType;
            }

            if(expr->target->kind == ExprKind::Identifier){
                const std::string& name = expr->target->text;

                if(classes_.count(name)){
                    std::string ctorKey = name + "::init";
                    auto fit = functions_.find(ctorKey);
                    if(fit != functions_.end()){
                        checkCallArgs(fit->second, expr->args, name, expr->line, expr->column);
                    }else if(!expr->args.empty()){
                        error(expr->line, expr->column, "class '" + name + "' has no constructor accepting arguments");
                        for(Arg& a : expr->args) checkExpr(a.value.get());
                    }
                    return Type::makeClass(name);
                }

                auto fit = functions_.find(name);
                if(fit == functions_.end()){
                    bool isVar = false;
                    for(const Scope& s : scopes_){
                        if(s.vars.count(name)){ isVar = true; break; }
                    }
                    for(Arg& a : expr->args) checkExpr(a.value.get());
                    if(!isVar){
                        error(expr->line, expr->column, "undefined function '" + name + "'");
                    }
                    return Type::makeUnknown();
                }
                checkCallArgs(fit->second, expr->args, name, expr->line, expr->column);
                ensureFunctionChecked(name);
                return functions_.at(name).returnType;
            }

            checkExpr(expr->target.get());
            for(Arg& a : expr->args) checkExpr(a.value.get());
            return Type::makeUnknown();
        }

        case ExprKind::Index:{
            Type targetType = checkExpr(expr->target.get());
            Type indexType = checkExpr(expr->right.get());
            if(targetType.isUnknown()) return Type::makeUnknown();
            if(targetType.nullable){
                error(expr->line, expr->column, "value may be null; check for null before indexing");
            }
            if(targetType.kind == TypeKind::Array){
                if(!indexType.isUnknown() && indexType.kind != TypeKind::Int){
                    error(expr->line, expr->column, "array index must be int, got " + typeToString(indexType));
                }
                return *targetType.elementType;
            }
            if(targetType.kind == TypeKind::Map){
                if(!isAssignable(*targetType.mapKeyType, indexType)){
                    error(expr->line, expr->column, "map key must be " + typeToString(*targetType.mapKeyType)
                          + ", got " + typeToString(indexType));
                }
                return *targetType.mapValueType;
            }
            error(expr->line, expr->column, "cannot index type " + typeToString(targetType));
            return Type::makeUnknown();
        }

        case ExprKind::Property:{
            if(expr->target->kind == ExprKind::Identifier){
                const std::string& maybeName = expr->target->text;
                bool isVar = false;
                for(const Scope& s : scopes_){
                    if(s.vars.count(maybeName)){ isVar = true; break; }
                }
                if(!isVar && enums_.count(maybeName)){
                    const EnumInfo& info = enums_.at(maybeName);
                    if(!info.variants.count(expr->text)){
                        error(expr->line, expr->column, "enum '" + maybeName + "' has no variant '" + expr->text + "'");
                    }
                    return Type::makeEnum(maybeName);
                }
            }
            Type objType = checkExpr(expr->target.get());
            if(objType.isUnknown()) return Type::makeUnknown();
            if(objType.kind != TypeKind::Class){
                error(expr->line, expr->column, "cannot access property '" + expr->text
                      + "' on non-class type " + typeToString(objType));
                return Type::makeUnknown();
            }
            if(objType.nullable){
                error(expr->line, expr->column, "value may be null; check for null before accessing '" + expr->text + "'");
            }
            const Type* fieldType = findFieldType(objType.name, expr->text);
            if(!fieldType){
                error(expr->line, expr->column, "class '" + objType.name + "' has no field '" + expr->text + "'");
                return Type::makeUnknown();
            }
            return *fieldType;
        }

        case ExprKind::Unary:{
            Type operandType = checkExpr(expr->target.get());
            if(expr->text == "!"){
                if(!operandType.isUnknown() && operandType.kind != TypeKind::Bool){
                    error(expr->line, expr->column, "'!' requires bool, got " + typeToString(operandType));
                }
                return Type::makeBool();
            }
            //"-"
            if(!operandType.isUnknown() && !operandType.isNumeric()){
                error(expr->line, expr->column, "unary '-' requires a numeric type, got " + typeToString(operandType));
            }
            return operandType;
        }

        case ExprKind::Update:{
            Type operandType = checkExpr(expr->target.get());
            if(!isLvalue(expr->target.get())){
                error(expr->line, expr->column, "'" + expr->text + "' requires a variable, field, or index expression");
            }
            if(!operandType.isUnknown() && !operandType.isNumeric()){
                error(expr->line, expr->column, "'" + expr->text + "' requires a numeric type, got " + typeToString(operandType));
            }
            return operandType;
        }

        case ExprKind::Binary:{
            Type left = checkExpr(expr->target.get());
            Type right = checkExpr(expr->right.get());
            const std::string& op = expr->text;

            if(op == "+"){
                bool leftIsStr = (left.kind == TypeKind::Str);
                bool rightIsStr = (right.kind == TypeKind::Str);
                if(leftIsStr && rightIsStr){
                    if(left.nullable || right.nullable){
                        error(expr->line, expr->column, "value may be null; check for null before using '+' on a str?");
                    }
                    return Type::makeStr();
                }
                if(leftIsStr || rightIsStr){
                    error(expr->line, expr->column, "'+' requires both operands to be str (concatenation) or both numeric (addition), got "
                          + typeToString(left) + " and " + typeToString(right));
                    return Type::makeUnknown();
                }
                if(!left.isUnknown() && !left.isNumeric()){
                    error(expr->line, expr->column, "'+' requires numeric or str operands, got " + typeToString(left));
                }
                if(!right.isUnknown() && !right.isNumeric()){
                    error(expr->line, expr->column, "'+' requires numeric or str operands, got " + typeToString(right));
                }
                if(left.kind == TypeKind::Float || right.kind == TypeKind::Float) return Type::makeFloat();
                return Type::makeInt();
            }
            if(op == "-" || op == "*" || op == "/" || op == "%" || op == "**"){
                if(!left.isUnknown() && !left.isNumeric()){
                    error(expr->line, expr->column, "'" + op + "' requires numeric operands, got " + typeToString(left));
                }
                if(!right.isUnknown() && !right.isNumeric()){
                    error(expr->line, expr->column, "'" + op + "' requires numeric operands, got " + typeToString(right));
                }
                if(left.kind == TypeKind::Float || right.kind == TypeKind::Float) return Type::makeFloat();
                return Type::makeInt();
            }
            if(op == "<" || op == ">" || op == "<=" || op == ">="){
                if((!left.isUnknown() && !left.isNumeric()) || (!right.isUnknown() && !right.isNumeric())){
                    error(expr->line, expr->column, "'" + op + "' requires numeric operands");
                }
                return Type::makeBool();
            }
            if(op == "==" || op == "!="){
                if(!left.isUnknown() && !right.isUnknown()
                   && left.kind != TypeKind::Null && right.kind != TypeKind::Null
                   && !sameBaseType(left, right) && !(left.isNumeric() && right.isNumeric())){
                    error(expr->line, expr->column, "cannot compare " + typeToString(left) + " and " + typeToString(right));
                }
                return Type::makeBool();
            }
            //"&&" / "||"
            if((!left.isUnknown() && left.kind != TypeKind::Bool) || (!right.isUnknown() && right.kind != TypeKind::Bool)){
                error(expr->line, expr->column, "'" + op + "' requires bool operands");
            }
            return Type::makeBool();
        }

        case ExprKind::Assignment:{
            Type valueType = checkExpr(expr->right.get());
            if(!isLvalue(expr->target.get())){
                error(expr->line, expr->column, "left-hand side of '" + expr->text + "' is not assignable");
                return valueType;
            }
            if(expr->target->kind == ExprKind::Identifier){
                for(auto it = scopes_.rbegin(); it != scopes_.rend(); ++it){
                    auto vit = it->vars.find(expr->target->text);
                    if(vit != it->vars.end()){
                        if(vit->second.isConst){
                            error(expr->line, expr->column, "cannot assign to const '" + expr->target->text + "'");
                        }
                        break;
                    }
                }
            }
            Type targetType = checkExpr(expr->target.get());
            if(expr->text == "="){
                if(!isAssignable(targetType, valueType)){
                    error(expr->line, expr->column, "cannot assign " + typeToString(valueType) + " to " + typeToString(targetType));
                }
            }else{
                //+= -= *= /=  require numeric on both sides, same widening rule as assignment
                if((!targetType.isUnknown() && !targetType.isNumeric()) || (!valueType.isUnknown() && !valueType.isNumeric())){
                    error(expr->line, expr->column, "'" + expr->text + "' requires numeric operands");
                }else if(targetType.kind == TypeKind::Int && valueType.kind == TypeKind::Float){
                    error(expr->line, expr->column, "cannot use '" + expr->text + "' with a float on an int variable");
                }
            }
            //assignment narrows the target back to non-null when assigning a non-null value
            if(expr->target->kind == ExprKind::Identifier && !valueType.nullable){
                narrowNonNull(expr->target->text);
            }
            return targetType;
        }
    }
    return Type::makeUnknown();
}

void Checker::check(const Program& program){
    collectDeclarations(program);
    pushScope();
    checkStmtList(program);
    popScope();
}

}//namespace zinc
