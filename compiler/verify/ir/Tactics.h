#pragma once

#include <verify/ir/Function.h>

namespace verify::ir {

//! Whether the proposition 'prop' has a shape that 'tactic' proves
/*!
Only the tactics that state a fixed clause are decided here. Everything else, including the
tactics whose clause is variadic, has no shape to compare against and is rejected.
*/
bool provesProp(const Function& function, Tactic tactic, Bool prop);

}
