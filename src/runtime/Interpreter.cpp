#include "runtime/Interpreter.h"

#include <cmath>

#include "runtime/Builtins.h"
#include "runtime/RuntimeError.h"

namespace zinc{

namespace{
double asDouble(const Value& v){
    return v.kind == ValueKind::Float ? v.floatValue : static_cast<double>(v.intValue);
}
}//namespace

Interpreter::Interpreter(){
    registerBuiltins(builtinRegistry_);
}

void Interpreter::error(size_t line, size_t column, const std::string& message){
    throw RuntimeException{RuntimeError{message, line, column}};
}

//== declaration collection ===================================================

void Interpreter::collectDeclarations(const Program& program){
    for(const auto& stmt : program){
        if(stmt->kind == StmtKind::EnumDecl){
            EnumInfo info;
            for(const std::string& v : stmt->enumVariants) info.variants.insert(v);
            enums_[stmt->name] = std::move(info);
        }else if(stmt->kind == StmtKind::ClassDecl){
            ClassInfo info;
            info.name = stmt->name;
            info.baseClassName = stmt->baseClassName;
            for(const ClassMemberNode& member : stmt->members){
                if(member.kind == ClassMemberNode::Kind::Var){
                    info.fieldInits.emplace_back(member.name, member.initExpr.get());
                }else if(member.kind == ClassMemberNode::Kind::Fn){
                    MethodInfo m;
                    for(const Param& p : member.params){
                        m.paramNames.push_back(p.name);
                        m.paramIsRef.push_back(p.isRef);
                    }
                    m.body = &member.body;
                    info.methods[member.name] = std::move(m);
                }else{ //Ctor
                    MethodInfo m;
                    for(const Param& p : member.params){
                        m.paramNames.push_back(p.name);
                        m.paramIsRef.push_back(p.isRef);
                    }
                    m.body = &member.body;
                    info.ctor = std::move(m);
                    info.hasCtor = true;
                }
            }
            classes_[stmt->name] = std::move(info);
        }
    }

    //bind top-level functions into globals so forward references/recursion work
    for(const auto& stmt : program){
        if(stmt->kind == StmtKind::FnDecl){
            auto fn = std::make_shared<FunctionValue>();
            fn->name = stmt->name;
            for(const Param& p : stmt->params){
                fn->paramNames.push_back(p.name);
                fn->paramIsRef.push_back(p.isRef);
            }
            fn->isBlockBody = true;
            fn->bodyBlock = &stmt->body;
            fn->closureEnv = globals_;
            Value v;
            v.kind = ValueKind::Function;
            v.functionValue = fn;
            globals_->define(stmt->name, v);
        }
    }
}

const Interpreter::ClassInfo* Interpreter::findClass(const std::string& name) const{
    auto it = classes_.find(name);
    return it == classes_.end() ? nullptr : &it->second;
}

const Interpreter::MethodInfo* Interpreter::findMethod(const std::string& className, const std::string& methodName, std::string& ownerClassOut) const{
    std::string cur = className;
    while(!cur.empty()){
        const ClassInfo* info = findClass(cur);
        if(!info) break;
        auto it = info->methods.find(methodName);
        if(it != info->methods.end()){
            ownerClassOut = cur;
            return &it->second;
        }
        cur = info->baseClassName;
    }
    return nullptr;
}

//== top level =================================================================

void Interpreter::run(const Program& program){
    globals_ = std::make_shared<Environment>();
    currentEnv_ = globals_;
    collectDeclarations(program);
    try{
        execStmtList(program);
    }catch(ReturnSignal&){
        //'return' outside any function - the checker should already reject this;
        //swallow defensively rather than crashing the process
    }catch(BreakSignal&){
        //likewise for a stray top-level 'break'
    }
}

//== statements ================================================================

void Interpreter::execStmtList(const std::vector<std::unique_ptr<Stmt>>& stmts){
    for(const auto& s : stmts){
        execStmt(s.get());
    }
}

void Interpreter::execStmt(Stmt* stmt){
    switch(stmt->kind){
        case StmtKind::Import:
            if(stmt->isNamedImport){
                for(const std::string& name : stmt->importNames){
                    auto it = builtinRegistry_.find(name);
                    if(it == builtinRegistry_.end()){
                        error(stmt->line, stmt->column, "unknown import '" + name + "' (no such standard library export)");
                    }
                    currentEnv_->define(name, it->second);
                }
            }
            break;

        case StmtKind::EnumDecl:
        case StmtKind::ClassDecl:
        case StmtKind::FnDecl:
            break; //already handled by collectDeclarations

        case StmtKind::VarDecl:{
            Value v = evalExpr(stmt->initExpr.get());
            currentEnv_->define(stmt->name, v);
            break;
        }

        case StmtKind::ExprStmt:
            evalExpr(stmt->expr.get());
            break;

        case StmtKind::Switch:{
            Value subject = evalExpr(stmt->subject.get());
            try{
                bool matched = false;
                for(CaseClauseNode& c : stmt->cases){
                    if(!matched){
                        Value caseVal = evalExpr(c.value.get());
                        if(valuesEqual(subject, caseVal)) matched = true;
                    }
                    if(matched){
                        execStmtList(c.body); //C-style fallthrough unless a case hits 'break'
                    }
                }
                if(stmt->hasDefault){
                    execStmtList(stmt->defaultBody); //grammar guarantees default is always last
                }
            }catch(BreakSignal&){}
            break;
        }

        case StmtKind::While:{
            try{
                while(evalExpr(stmt->condition.get()).isTruthy()){
                    ScopedEnv guard(*this, std::make_shared<Environment>(currentEnv_));
                    execStmtList(stmt->bodyBlock);
                }
            }catch(BreakSignal&){}
            break;
        }

        case StmtKind::ForIn:{
            Value iterable = evalExpr(stmt->iterable.get());
            if(iterable.kind != ValueKind::Array){
                error(stmt->line, stmt->column, "for-in requires an array at runtime");
            }
            std::vector<Value> items = *iterable.arrayValue; //snapshot, so mutating the array mid-loop is well-defined
            try{
                for(Value& item : items){
                    ScopedEnv guard(*this, std::make_shared<Environment>(currentEnv_));
                    currentEnv_->define(stmt->loopVarName, item);
                    execStmtList(stmt->bodyBlock);
                }
            }catch(BreakSignal&){}
            break;
        }

        case StmtKind::ForC:{
            ScopedEnv outerGuard(*this, std::make_shared<Environment>(currentEnv_));
            execStmt(stmt->forInit.get());
            try{
                while(evalExpr(stmt->condition.get()).isTruthy()){
                    {
                        ScopedEnv bodyGuard(*this, std::make_shared<Environment>(currentEnv_));
                        execStmtList(stmt->bodyBlock);
                    }
                    evalExpr(stmt->forIncrement.get());
                }
            }catch(BreakSignal&){}
            break;
        }

        case StmtKind::Return:{
            Value v = stmt->returnValue ? evalExpr(stmt->returnValue.get()) : Value::makeNull();
            throw ReturnSignal{v};
        }

        case StmtKind::Break:
            throw BreakSignal{};
    }
}

//== calls ======================================================================

Value Interpreter::callFunction(const FunctionValue& fn, std::vector<Value>& args, const Value* thisValue, size_t line, size_t column){
    if(args.size() != fn.paramNames.size()){
        error(line, column, "'" + fn.name + "' expects " + std::to_string(fn.paramNames.size())
              + " argument(s), got " + std::to_string(args.size()));
    }

    auto callEnv = std::make_shared<Environment>(fn.closureEnv);
    if(thisValue){
        callEnv->define("this", *thisValue);
    }
    for(size_t i = 0; i < fn.paramNames.size(); i++){
        callEnv->define(fn.paramNames[i], args[i]);
    }

    ScopedEnv guard(*this, callEnv);
    Value result = Value::makeNull();
    try{
        if(fn.isBlockBody){
            execStmtList(*fn.bodyBlock);
        }else{
            result = evalExpr(const_cast<Expr*>(fn.bodyExpr)); //expr-body lambda: implicit return of the expr's value
        }
    }catch(ReturnSignal& ret){
        result = ret.value;
    }

    //write ref params' final values back into the (now-finished) call env
    for(size_t i = 0; i < fn.paramNames.size(); i++){
        if(fn.paramIsRef[i]){
            if(Value* v = callEnv->find(fn.paramNames[i])){
                args[i] = *v;
            }
        }
    }
    return result;
}

Value Interpreter::callNative(const Value& fnVal, std::vector<Value>& args, size_t line, size_t column){
    return fnVal.nativeFn(*this, args, line, column);
}

Value Interpreter::instantiateClass(const std::string& className, std::vector<Arg>& argExprs, size_t line, size_t column){
    auto instance = std::make_shared<Instance>();
    instance->className = className;

    //walk from the root base down to the derived class, so base fields are
    //initialized first and later classes' inits can see `this`
    std::vector<const ClassInfo*> chain;
    for(std::string cur = className; !cur.empty();){
        const ClassInfo* info = findClass(cur);
        if(!info) break;
        chain.push_back(info);
        cur = info->baseClassName;
    }

    Value thisVal;
    thisVal.kind = ValueKind::Instance;
    thisVal.instanceValue = instance;

    for(auto it = chain.rbegin(); it != chain.rend(); ++it){
        ScopedEnv guard(*this, std::make_shared<Environment>(globals_));
        currentEnv_->define("this", thisVal);
        for(const auto& [fieldName, initExpr] : (*it)->fieldInits){
            instance->fields[fieldName] = evalExpr(const_cast<Expr*>(initExpr));
        }
    }

    //no super()/constructor-chaining syntax has been decided, so only the
    //most-derived class's own constructor (if any) runs
    const ClassInfo* ctorOwner = nullptr;
    for(std::string cur = className; !cur.empty();){
        const ClassInfo* info = findClass(cur);
        if(!info) break;
        if(info->hasCtor){ ctorOwner = info; break; }
        cur = info->baseClassName;
    }
    if(ctorOwner){
        std::vector<Value> args;
        for(Arg& a : argExprs) args.push_back(evalExpr(a.value.get()));

        FunctionValue ctorFn;
        ctorFn.name = className + "::init";
        ctorFn.paramNames = ctorOwner->ctor.paramNames;
        ctorFn.paramIsRef = ctorOwner->ctor.paramIsRef;
        ctorFn.isBlockBody = true;
        ctorFn.bodyBlock = ctorOwner->ctor.body;
        ctorFn.closureEnv = globals_;
        callFunction(ctorFn, args, &thisVal, line, column);

        for(size_t i = 0; i < ctorFn.paramIsRef.size() && i < argExprs.size(); i++){
            if(ctorFn.paramIsRef[i]) evalLvalueSet(argExprs[i].value.get(), args[i]);
        }
    }

    return thisVal;
}

//== lvalues ====================================================================

void Interpreter::evalLvalueSet(Expr* target, Value value){
    if(target->kind == ExprKind::Identifier){
        if(!currentEnv_->assign(target->text, value)){
            error(target->line, target->column, "undefined identifier '" + target->text + "'");
        }
        return;
    }
    if(target->kind == ExprKind::Property){
        Value obj = evalExpr(target->target.get());
        if(obj.kind != ValueKind::Instance){
            error(target->line, target->column, "cannot set a property on a non-instance value");
        }
        obj.instanceValue->fields[target->text] = value;
        return;
    }
    if(target->kind == ExprKind::Index){
        Value obj = evalExpr(target->target.get());
        Value idx = evalExpr(target->right.get());
        if(obj.kind == ValueKind::Array){
            long long i = idx.intValue;
            if(i < 0 || static_cast<size_t>(i) >= obj.arrayValue->size()){
                error(target->line, target->column, "array index out of bounds");
            }
            (*obj.arrayValue)[static_cast<size_t>(i)] = value;
            return;
        }
        if(obj.kind == ValueKind::Map){
            (*obj.mapValue)[idx.strValue] = value;
            return;
        }
        error(target->line, target->column, "cannot index-assign this value");
    }
    error(target->line, target->column, "invalid assignment target");
}

//== expressions ================================================================

Value Interpreter::arithmeticOp(const Value& l, const Value& r, const std::string& op, size_t line, size_t column){
    bool useFloat = (l.kind == ValueKind::Float || r.kind == ValueKind::Float);
    if(op == "**"){
        double result = std::pow(asDouble(l), asDouble(r));
        return useFloat ? Value::makeFloat(result) : Value::makeInt(static_cast<long long>(result));
    }
    if(useFloat){
        double a = asDouble(l), b = asDouble(r);
        if(op == "+") return Value::makeFloat(a + b);
        if(op == "-") return Value::makeFloat(a - b);
        if(op == "*") return Value::makeFloat(a * b);
        if(op == "/") return Value::makeFloat(a / b); //IEEE754 inf/nan on zero divisor, not a runtime error
        if(op == "%") return Value::makeFloat(std::fmod(a, b));
    }else{
        long long a = l.intValue, b = r.intValue;
        if(op == "+") return Value::makeInt(a + b);
        if(op == "-") return Value::makeInt(a - b);
        if(op == "*") return Value::makeInt(a * b);
        if(op == "/"){
            if(b == 0) error(line, column, "division by zero");
            return Value::makeInt(a / b);
        }
        if(op == "%"){
            if(b == 0) error(line, column, "modulo by zero");
            return Value::makeInt(a % b);
        }
    }
    error(line, column, "unknown arithmetic operator '" + op + "'");
}

Value Interpreter::comparisonOp(const Value& l, const Value& r, const std::string& op){
    double a = asDouble(l), b = asDouble(r);
    if(op == "<") return Value::makeBool(a < b);
    if(op == ">") return Value::makeBool(a > b);
    if(op == "<=") return Value::makeBool(a <= b);
    return Value::makeBool(a >= b);
}

bool Interpreter::valuesEqual(const Value& a, const Value& b){
    if(a.kind == ValueKind::Null || b.kind == ValueKind::Null) return a.kind == b.kind;
    bool aNum = a.kind == ValueKind::Int || a.kind == ValueKind::Float;
    bool bNum = b.kind == ValueKind::Int || b.kind == ValueKind::Float;
    if(aNum && bNum) return asDouble(a) == asDouble(b);
    if(a.kind != b.kind) return false;
    switch(a.kind){
        case ValueKind::Bool: return a.boolValue == b.boolValue;
        case ValueKind::Str: return a.strValue == b.strValue;
        case ValueKind::EnumVariant: return a.enumName == b.enumName && a.enumVariant == b.enumVariant;
        case ValueKind::Instance: return a.instanceValue == b.instanceValue; //identity - no deep equality decided
        case ValueKind::Array: return a.arrayValue == b.arrayValue;         //identity
        case ValueKind::Map: return a.mapValue == b.mapValue;               //identity
        default: return false;
    }
}

Value Interpreter::evalExpr(Expr* expr){
    switch(expr->kind){
        case ExprKind::IntLiteral: return Value::makeInt(std::stoll(expr->text));
        case ExprKind::FloatLiteral: return Value::makeFloat(std::stod(expr->text));
        case ExprKind::StringLiteral: return Value::makeStr(expr->text);
        case ExprKind::BoolLiteral: return Value::makeBool(expr->boolValue);
        case ExprKind::NullLiteral: return Value::makeNull();

        case ExprKind::Identifier:{
            Value* slot = currentEnv_->find(expr->text);
            if(!slot){
                error(expr->line, expr->column, "undefined identifier '" + expr->text + "'");
            }
            return *slot;
        }

        case ExprKind::This:{
            Value* slot = currentEnv_->find("this");
            if(!slot){
                error(expr->line, expr->column, "'this' used outside of a method");
            }
            return *slot;
        }

        case ExprKind::TemplateString:{
            std::string out;
            for(TemplatePart& part : expr->templateParts){
                out += part.isExpr ? stringifyValue(evalExpr(part.expr.get())) : part.text;
            }
            return Value::makeStr(out);
        }

        case ExprKind::ArrayLiteral:{
            std::vector<Value> elems;
            for(auto& e : expr->elements) elems.push_back(evalExpr(e.get()));
            return Value::makeArray(std::move(elems));
        }

        case ExprKind::MapLiteral:{
            std::unordered_map<std::string, Value> entries;
            for(MapEntryNode& e : expr->mapEntries) entries[e.key] = evalExpr(e.value.get());
            return Value::makeMap(std::move(entries));
        }

        case ExprKind::Lambda:{
            auto fn = std::make_shared<FunctionValue>();
            fn->name = "<lambda>";
            fn->paramNames = expr->lambdaParams;
            fn->paramIsRef.assign(expr->lambdaParams.size(), false); //lambda params are always by-value per grammar
            fn->isBlockBody = expr->isBlockBody;
            if(expr->isBlockBody){
                fn->bodyBlock = &expr->block;
            }else{
                fn->bodyExpr = expr->bodyExpr.get();
            }
            fn->closureEnv = currentEnv_; //capture the defining scope
            Value v;
            v.kind = ValueKind::Function;
            v.functionValue = fn;
            return v;
        }

        case ExprKind::If:{
            //FIXME (see FIXME.md): block "value" semantics for if/try-as-
            //expressions are undecided. the null returned below is an
            //internal placeholder only - NOT an established Zinc rule.
            if(evalExpr(expr->condition.get()).isTruthy()){
                execStmtList(expr->block);
            }else if(expr->hasElse){
                if(expr->elseIsIf) evalExpr(expr->elseIf.get());
                else execStmtList(expr->elseBlock);
            }
            return Value::makeNull();
        }

        case ExprKind::Try:{
            //same FIXME as If applies here
            try{
                execStmtList(expr->block);
            }catch(RuntimeException& e){
                ScopedEnv guard(*this, std::make_shared<Environment>(currentEnv_));
                currentEnv_->define(expr->catchName, Value::makeStr(e.error.message));
                execStmtList(expr->catchBlock);
            }
            return Value::makeNull();
        }

        case ExprKind::Call:{
            if(expr->target->kind == ExprKind::Property){
                Expr* propNode = expr->target.get();
                Value obj = evalExpr(propNode->target.get());

                if(obj.kind == ValueKind::Namespace){
                    auto it = obj.namespaceValue->members.find(propNode->text);
                    if(it == obj.namespaceValue->members.end()){
                        error(expr->line, expr->column, "namespace '" + obj.namespaceValue->name + "' has no member '" + propNode->text + "'");
                    }
                    std::vector<Value> args;
                    for(Arg& a : expr->args) args.push_back(evalExpr(a.value.get()));
                    return callNative(it->second, args, expr->line, expr->column);
                }
                if(obj.kind == ValueKind::Instance){
                    std::string ownerClass;
                    const MethodInfo* method = findMethod(obj.instanceValue->className, propNode->text, ownerClass);
                    if(!method){
                        error(expr->line, expr->column, "'" + obj.instanceValue->className + "' has no method '" + propNode->text + "'");
                    }
                    std::vector<Value> args;
                    for(Arg& a : expr->args) args.push_back(evalExpr(a.value.get()));
                    FunctionValue fn;
                    fn.name = ownerClass + "::" + propNode->text;
                    fn.paramNames = method->paramNames;
                    fn.paramIsRef = method->paramIsRef;
                    fn.isBlockBody = true;
                    fn.bodyBlock = method->body;
                    fn.closureEnv = globals_;
                    Value result = callFunction(fn, args, &obj, expr->line, expr->column);
                    for(size_t i = 0; i < fn.paramIsRef.size() && i < expr->args.size(); i++){
                        if(fn.paramIsRef[i]) evalLvalueSet(expr->args[i].value.get(), args[i]);
                    }
                    return result;
                }
                error(expr->line, expr->column, "cannot call method '" + propNode->text + "' on this value");
            }

            if(expr->target->kind == ExprKind::Identifier){
                const std::string& name = expr->target->text;
                if(classes_.count(name)){
                    return instantiateClass(name, expr->args, expr->line, expr->column);
                }

                Value* slot = currentEnv_->find(name);
                if(!slot){
                    error(expr->line, expr->column, "undefined function '" + name + "'");
                }
                Value callee = *slot;
                std::vector<Value> args;
                for(Arg& a : expr->args) args.push_back(evalExpr(a.value.get()));

                if(callee.kind == ValueKind::NativeFunction){
                    return callNative(callee, args, expr->line, expr->column);
                }
                if(callee.kind == ValueKind::Function){
                    Value result = callFunction(*callee.functionValue, args, nullptr, expr->line, expr->column);
                    for(size_t i = 0; i < callee.functionValue->paramIsRef.size() && i < expr->args.size(); i++){
                        if(callee.functionValue->paramIsRef[i]) evalLvalueSet(expr->args[i].value.get(), args[i]);
                    }
                    return result;
                }
                error(expr->line, expr->column, "'" + name + "' is not callable");
            }

            Value callee = evalExpr(expr->target.get());
            std::vector<Value> args;
            for(Arg& a : expr->args) args.push_back(evalExpr(a.value.get()));
            if(callee.kind == ValueKind::Function) return callFunction(*callee.functionValue, args, nullptr, expr->line, expr->column);
            if(callee.kind == ValueKind::NativeFunction) return callNative(callee, args, expr->line, expr->column);
            error(expr->line, expr->column, "value is not callable");
        }

        case ExprKind::Index:{
            Value target = evalExpr(expr->target.get());
            Value idx = evalExpr(expr->right.get());
            if(target.kind == ValueKind::Array){
                long long i = idx.intValue;
                if(i < 0 || static_cast<size_t>(i) >= target.arrayValue->size()){
                    error(expr->line, expr->column, "array index out of bounds");
                }
                return (*target.arrayValue)[static_cast<size_t>(i)];
            }
            if(target.kind == ValueKind::Map){
                auto it = target.mapValue->find(idx.strValue);
                if(it == target.mapValue->end()){
                    error(expr->line, expr->column, "map has no key '" + idx.strValue + "'");
                }
                return it->second;
            }
            error(expr->line, expr->column, "cannot index this value at runtime");
        }

        case ExprKind::Property:{
            if(expr->target->kind == ExprKind::Identifier){
                const std::string& maybeName = expr->target->text;
                if(!currentEnv_->find(maybeName) && enums_.count(maybeName)){
                    return Value::makeEnumVariant(maybeName, expr->text);
                }
            }
            Value obj = evalExpr(expr->target.get());
            if(obj.kind == ValueKind::Namespace){
                auto it = obj.namespaceValue->members.find(expr->text);
                if(it == obj.namespaceValue->members.end()){
                    error(expr->line, expr->column, "namespace '" + obj.namespaceValue->name + "' has no member '" + expr->text + "'");
                }
                return it->second;
            }
            if(obj.kind == ValueKind::Instance){
                auto it = obj.instanceValue->fields.find(expr->text);
                if(it == obj.instanceValue->fields.end()){
                    error(expr->line, expr->column, "'" + obj.instanceValue->className + "' has no field '" + expr->text + "'");
                }
                return it->second;
            }
            error(expr->line, expr->column, "cannot access property '" + expr->text + "' on this value");
        }

        case ExprKind::Unary:{
            Value v = evalExpr(expr->target.get());
            if(expr->text == "!") return Value::makeBool(!v.isTruthy());
            if(v.kind == ValueKind::Float) return Value::makeFloat(-v.floatValue);
            return Value::makeInt(-v.intValue);
        }

        case ExprKind::Update:{
            Value oldVal = evalExpr(expr->target.get());
            Value updated = oldVal;
            long long delta = (expr->text == "++") ? 1 : -1;
            if(updated.kind == ValueKind::Float) updated.floatValue += static_cast<double>(delta);
            else updated.intValue += delta;
            evalLvalueSet(expr->target.get(), updated);
            return expr->isPrefix ? updated : oldVal;
        }

        case ExprKind::Binary:{
            if(expr->text == "&&"){
                if(!evalExpr(expr->target.get()).isTruthy()) return Value::makeBool(false);
                return Value::makeBool(evalExpr(expr->right.get()).isTruthy());
            }
            if(expr->text == "||"){
                if(evalExpr(expr->target.get()).isTruthy()) return Value::makeBool(true);
                return Value::makeBool(evalExpr(expr->right.get()).isTruthy());
            }

            Value l = evalExpr(expr->target.get());
            Value r = evalExpr(expr->right.get());
            const std::string& op = expr->text;

            if(op == "+"){
                if(l.kind == ValueKind::Str || r.kind == ValueKind::Str){
                    //checker guarantees both sides are str when either is
                    return Value::makeStr(l.strValue + r.strValue);
                }
                return arithmeticOp(l, r, op, expr->line, expr->column);
            }
            if(op == "-" || op == "*" || op == "/" || op == "%" || op == "**"){
                return arithmeticOp(l, r, op, expr->line, expr->column);
            }
            if(op == "<" || op == ">" || op == "<=" || op == ">="){
                return comparisonOp(l, r, op);
            }
            //"==" / "!="
            bool eq = valuesEqual(l, r);
            return Value::makeBool(op == "==" ? eq : !eq);
        }

        case ExprKind::Assignment:{
            Value rhs = evalExpr(expr->right.get());
            if(expr->text == "="){
                evalLvalueSet(expr->target.get(), rhs);
                return rhs;
            }
            Value cur = evalExpr(expr->target.get());
            std::string op(1, expr->text[0]); // "+=" -> "+"
            Value updated = arithmeticOp(cur, rhs, op, expr->line, expr->column);
            evalLvalueSet(expr->target.get(), updated);
            return updated;
        }
    }
    return Value::makeNull();
}

}//namespace zinc