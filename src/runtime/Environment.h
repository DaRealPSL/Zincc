#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "runtime/Value.h"

namespace zinc{

/**
 * a lexical scope: a flat name->Value table plus a link to the enclosing
 * scope. shared_ptr-held (not a stack-allocated vector like the checker's
 * Scope) because closures (lambdas, functions) need to keep their defining
 * environment alive after the C++ call frame that created it returns.
 */
class Environment : public std::enable_shared_from_this<Environment>{
public:
    explicit Environment(std::shared_ptr<Environment> parent = nullptr): parent_(std::move(parent)){}

    /** binds a new name in *this* scope (shadowing any outer binding of the same name) */
    void define(const std::string& name, Value value){
        vars_[name] = std::move(value);
    }

    /** looks up a name, walking outward through parent scopes */
    Value* find(const std::string& name){
        auto it = vars_.find(name);
        if(it != vars_.end()) return &it->second;
        return parent_ ? parent_->find(name) : nullptr;
    }

    /** assigns to an existing binding (walking outward); returns false if not found anywhere */
    bool assign(const std::string& name, Value value){
        auto it = vars_.find(name);
        if(it != vars_.end()){
            it->second = std::move(value);
            return true;
        }
        return parent_ ? parent_->assign(name, std::move(value)) : false;
    }

private:
    std::shared_ptr<Environment> parent_;
    std::unordered_map<std::string, Value> vars_;
};

}//namespace zinc