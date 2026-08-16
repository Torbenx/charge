#pragma once

#include <verify/language/Parser.h>

#include <string>

namespace verify::language {

//! Writes a parsed function back out in the text form of the IR
/*!
The output parses to the same function again. Everything the IR refers to but the source left
unnamed is given a name, and formatting the result a second time reproduces it unchanged.
*/
std::string format(const ParsedFunction&);

}
