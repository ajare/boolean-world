#include "core/BuildVariables.h"

#include <array>
#include <cmath>
#include <format>
#include <set>

#include "core/CoreException.h"
#include "core/Serializer.h"

namespace bw {
namespace core {

using namespace std;

BuildVariableType buildVariableType(BuildVariableValue const& value) {
  if (holds_alternative<string>(value)) return BuildVariableType::String;
  if (holds_alternative<int64_t>(value)) return BuildVariableType::Integer;
  if (holds_alternative<double>(value)) return BuildVariableType::Float;
  return BuildVariableType::Boolean;
}

char const* buildVariableTypeName(BuildVariableType type) {
  switch (type) {
    case BuildVariableType::String: return "string";
    case BuildVariableType::Integer: return "integer";
    case BuildVariableType::Float: return "float";
    case BuildVariableType::Boolean: return "boolean";
  }
  return "unknown";
}

BuildVariableValue defaultBuildVariableValue(BuildVariableType type) {
  switch (type) {
    case BuildVariableType::String: return string{};
    case BuildVariableType::Integer: return int64_t{0};
    case BuildVariableType::Float: return 0.0;
    case BuildVariableType::Boolean: return false;
  }
  return string{};
}

bool isValidBuildVariableName(string const& name) {
  if (name.empty() ||
      !((name.front() >= 'A' && name.front() <= 'Z') ||
        (name.front() >= 'a' && name.front() <= 'z') ||
        name.front() == '_')) {
    return false;
  }
  for (auto character : name) {
    if (!((character >= 'A' && character <= 'Z') ||
          (character >= 'a' && character <= 'z') ||
          (character >= '0' && character <= '9') || character == '_')) {
      return false;
    }
  }
  static set<string> const keywords{
      "and", "break", "do", "else", "elseif", "end", "false", "for",
      "function", "goto", "if", "in", "local", "nil", "not", "or",
      "repeat", "return", "then", "true", "until", "while"};
  return !keywords.contains(name);
}

void validateBuildVariables(BuildVariables const& variables, string const& scope) {
  for (auto const& [name, value] : variables) {
    if (!isValidBuildVariableName(name)) {
      throw CoreException(format(
          "{} build variable '{}' is not a valid Lua identifier", scope, name));
    }
    if (auto const* number = get_if<double>(&value); number && !isfinite(*number)) {
      throw CoreException(format(
          "{} build variable '{}' must be a finite float", scope, name));
    }
    if (auto const* text = get_if<string>(&value);
        text && text->find('\0') != string::npos) {
      throw CoreException(format(
          "{} build variable '{}' contains an embedded NUL", scope, name));
    }
  }
}

void serializeBuildVariables(
    shared_ptr<Serializer> serializer, BuildVariables const& variables,
    string const& field) {
  serializer->beginArray(field);
  for (auto const& [name, value] : variables) {
    serializer->beginMap("");
    serializer->writeString("name", name);
    serializer->writeString("type", buildVariableTypeName(buildVariableType(value)));
    visit([&](auto const& concrete) {
      using Value = decay_t<decltype(concrete)>;
      if constexpr (is_same_v<Value, string>)
        serializer->writeString("value", concrete);
      else if constexpr (is_same_v<Value, int64_t>)
        serializer->writeInt64("value", concrete);
      else if constexpr (is_same_v<Value, double>)
        serializer->writeDouble("value", concrete);
      else
        serializer->writeBool("value", concrete);
    },
          value);
    serializer->endMap();
  }
  serializer->endArray();
}

BuildVariables deserializeBuildVariables(
    shared_ptr<Serializer> serializer, string const& field,
    bool acceptLegacyNumber) {
  BuildVariables result;
  if (!serializer->isPositional() && !serializer->hasField(field)) return result;

  serializer->beginArray(field);
  while (serializer->nextArrayItem()) {
    serializer->beginMap("");
    auto const name = serializer->readString("name");
    auto const type = serializer->readString("type");
    BuildVariableValue value;
    if (type == "string")
      value = serializer->readString("value");
    else if (type == "integer")
      value = serializer->readInt64("value");
    else if (type == "float" || (acceptLegacyNumber && type == "number"))
      value = serializer->readDouble("value");
    else if (type == "boolean")
      value = serializer->readBool("value");
    else
      throw CoreException(format(
          "Build variable '{}' has unknown serialized type '{}'", name, type));
    if (!result.emplace(name, move(value)).second) {
      throw CoreException(format("Duplicate build variable '{}'", name));
    }
    serializer->endMap();
  }
  serializer->endArray();
  validateBuildVariables(result, field);
  return result;
}

}  // namespace core
}  // namespace bw
