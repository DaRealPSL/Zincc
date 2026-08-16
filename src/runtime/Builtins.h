#pragma once

#include <string>
#include <unordered_map>

#include "runtime/Value.h"

namespace zinc{

/**
 * populates `registry` with every stdlib export available to `import { ... }`.
 * `print` is a bare NativeFunction value; `math` is a Namespace value holding
 * its own members (floor/sqrt/abs) as NativeFunctions. Both are resolved and
 * called through the exact same identifier/property/call machinery as any
 * user-defined function - nothing hbere is special-cased in the parser, AST,
 * or checker.
 */
void registerBuiltins(std::unordered_map<std::string, Value>& registry);
}//namespace zinc