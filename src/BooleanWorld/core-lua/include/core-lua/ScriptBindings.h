#pragma once

#include <sol/sol.hpp>

namespace bw {
namespace core {

// Registers the usertypes a script sees, on the state rather than on any one
// environment: a usertype belongs to the Lua state's registry, while the
// values a script can name are put in its environment by whoever is running
// it. Idempotent, so every execution can call it without cost.
//
// Primitives are bound by borrowed pointer only. Lua never owns a C++ object
// - the step that creates a Primitive owns it - so a script that raises
// halfway through cannot leak or double-free one.
void bindScriptTypes(sol::state& lua);

}  // namespace core
}  // namespace bw
