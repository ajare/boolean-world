#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace bw {
namespace core {

// The four manifest-authored kinds that a LuaScript may expose through its
// execution-local params table. Integer and number remain distinct so Lua gets
// an integer where the resource declared one and World serialization preserves
// that contract.
enum class ScriptParameterType {
  String,
  Integer,
  Number,
  Boolean,
};

using ScriptParameterValue =
    std::variant<std::string, int64_t, double, bool>;

struct ScriptParameterDefinition {
  std::string name;
  ScriptParameterType type{ScriptParameterType::String};
  ScriptParameterValue defaultValue{std::string{}};

  // Empty means free text. Non-empty means the String widget and value are
  // restricted to this ordered list.
  std::vector<std::string> choices;

  int64_t integerMinimum{0};
  int64_t integerMaximum{0};
  double numberMinimum{0.0};
  double numberMaximum{0.0};

  [[nodiscard]] bool accepts(ScriptParameterValue const& value) const;
};

[[nodiscard]] char const* scriptParameterTypeName(ScriptParameterType type);

}  // namespace core
}  // namespace bw
