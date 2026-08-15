#pragma once

#include <string>

#include "ast/Ast.h"

namespace zinc{

/** renders a whole program as an indented, human-readable tree */
std::string printProgram(const Program& program);

}//namespace zinc
