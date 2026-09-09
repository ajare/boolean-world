#pragma once

#include <map>
#include <memory>
#include <string>
#include <variant>

#include "core/Platform.h"

namespace bw {
namespace core {

class Serializer;

// An immutable, typed input made available to LayerBuildStep scripts. The
// variant alternative is the authoritative type; integers and floats remain
// distinct so Lua and serialization preserve the authored contract.
using BuildVariableValue = std::variant<std::string, int64_t, double, bool>;
using BuildVariables = std::map<std::string, BuildVariableValue>;

enum class BuildVariableType {
  String,
  Integer,
  Float,
  Boolean,
};

[[nodiscard]] BW_API BuildVariableType buildVariableType(
    BuildVariableValue const& value);
[[nodiscard]] BW_API char const* buildVariableTypeName(BuildVariableType type);
[[nodiscard]] BW_API BuildVariableValue defaultBuildVariableValue(
    BuildVariableType type);

// ASCII Lua identifier, excluding reserved words. This guarantees dot access
// through world.vars.name, layer.vars.name and step.vars.name.
[[nodiscard]] BW_API bool isValidBuildVariableName(std::string const& name);

BW_API void validateBuildVariables(
    BuildVariables const& variables, std::string const& scope);

BW_API void serializeBuildVariables(
    std::shared_ptr<Serializer> serializer, BuildVariables const& variables,
    std::string const& field = "vars");

// Reads a typed variable array. In keyed formats an absent field is empty.
// acceptLegacyNumber permits old RunScript data to spell float as number.
[[nodiscard]] BW_API BuildVariables deserializeBuildVariables(
    std::shared_ptr<Serializer> serializer, std::string const& field = "vars",
    bool acceptLegacyNumber = false);

}  // namespace core
}  // namespace bw
