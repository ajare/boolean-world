#include "core-lua/LuaScriptResource.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <memory>
#include <unordered_set>
#include <utility>

#include <willpower/application/resourcesystem/ResourceDefinitionFactory.h>
#include <willpower/application/resourcesystem/ResourceExceptions.h>
#include <willpower/application/resourcesystem/ResourceManager.h>
#include <willpower/application/resourcesystem/TextFileResource.h>

#include "core-lua/ScriptRuntime.h"

namespace bw {
namespace core {

using namespace std;
namespace resources = wp::application::resourcesystem;

namespace {

[[noreturn]] void parameterError(
    resources::Resource const* resource, string const& message) {
  throw resources::ResourceException(
      resource, "invalid LuaScript Params: " + message);
}

string requiredProperty(
    resources::Resource const* resource, wp::DataNode* node,
    string const& name) {
  string value;
  if (!node->getOptionalProperty(name, value)) {
    parameterError(resource, "Param is missing '" + name + "'");
  }
  return value;
}

int64_t parseInteger(
    resources::Resource const* resource, string const& parameter,
    string const& field, string const& text) {
  int64_t value{};
  auto const result = from_chars(text.data(), text.data() + text.size(), value);
  if (result.ec != errc{} || result.ptr != text.data() + text.size()) {
    parameterError(
        resource, "Param '" + parameter + "' has non-integer " + field +
                      " '" + text + "'");
  }
  return value;
}

double parseNumber(
    resources::Resource const* resource, string const& parameter,
    string const& field, string const& text) {
  double value{};
  auto const result = from_chars(
      text.data(), text.data() + text.size(), value, chars_format::general);
  if (result.ec != errc{} || result.ptr != text.data() + text.size() ||
      !isfinite(value)) {
    parameterError(
        resource, "Param '" + parameter + "' has non-number " + field +
                      " '" + text + "'");
  }
  return value;
}

class LuaScriptResourceDefinitionFactory final
    : public resources::ResourceDefinitionFactory {
public:
  LuaScriptResourceDefinitionFactory()
      : ResourceDefinitionFactory("LuaScript", "") {}

  void create(
      resources::Resource* resource, resources::ResourceManager*,
      wp::DataNode* node) override {
    auto* script = static_cast<LuaScriptResource*>(resource);
    vector<StepVariableDefinition> definitions;
    auto* vars = node->getOptionalChild("Vars");
    auto* legacyParams = node->getOptionalChild("Params");
    if (vars && legacyParams) {
      parameterError(resource, "Definition may not contain both Vars and legacy Params");
    }
    auto* collection = vars ? vars : legacyParams;
    if (!collection) {
      script->setStepVariableDefinitions({});
      return;
    }

    auto const* itemName = vars ? "Var" : "Param";
    collection->requireOnlyChildren({itemName});
    auto* parameter = collection->getOptionalChild(itemName);
    if (!parameter) {
      parameterError(resource, string(vars ? "Vars contains no Var" : "Params contains no Param"));
    }

    unordered_set<string> names;
    do {
      StepVariableDefinition definition;
      definition.name = requiredProperty(resource, parameter, "name");
      if (!isValidBuildVariableName(definition.name)) {
        parameterError(resource, "Step build variable name '" + definition.name +
                                     "' is not a valid Lua identifier");
      }
      if (!names.insert(definition.name).second) {
        parameterError(
            resource, "duplicate Step build variable name '" + definition.name + "'");
      }

      auto const authoredType =
          requiredProperty(resource, parameter, "type");
      auto type = authoredType;
      transform(
          type.begin(), type.end(), type.begin(),
          [](unsigned char character) { return static_cast<char>(tolower(character)); });
      auto const defaultText =
          requiredProperty(resource, parameter, "default");
      if (type == "string") {
        parameter->requireOnlyChildren(
            {"name", "type", "default", "Choices"});
        definition.type = BuildVariableType::String;
        definition.defaultValue = defaultText;
        if (auto* choices = parameter->getOptionalChild("Choices")) {
          choices->requireOnlyChildren({"Choice"});
          auto* choice = choices->getOptionalChild("Choice");
          if (!choice) {
            parameterError(
                resource, "Param '" + definition.name +
                              "' has an empty Choices list");
          }
          do {
            auto value = choice->getValue();
            if (find(
                    definition.choices.begin(), definition.choices.end(),
                    value) != definition.choices.end()) {
              parameterError(
                  resource, "Param '" + definition.name +
                                "' has duplicate Choice '" + value + "'");
            }
            definition.choices.push_back(move(value));
          } while (choice->next());
        }
      } else if (type == "integer") {
        parameter->requireOnlyChildren(
            {"name", "type", "default", "min", "max"});
        definition.type = BuildVariableType::Integer;
        definition.integerMinimum = parseInteger(
            resource, definition.name, "min",
            requiredProperty(resource, parameter, "min"));
        definition.integerMaximum = parseInteger(
            resource, definition.name, "max",
            requiredProperty(resource, parameter, "max"));
        definition.defaultValue = parseInteger(
            resource, definition.name, "default", defaultText);
      } else if (type == "float" || (legacyParams && type == "number")) {
        parameter->requireOnlyChildren(
            {"name", "type", "default", "min", "max"});
        definition.type = BuildVariableType::Float;
        definition.floatMinimum = parseNumber(
            resource, definition.name, "min",
            requiredProperty(resource, parameter, "min"));
        definition.floatMaximum = parseNumber(
            resource, definition.name, "max",
            requiredProperty(resource, parameter, "max"));
        definition.defaultValue = parseNumber(
            resource, definition.name, "default", defaultText);
      } else if (type == "boolean") {
        parameter->requireOnlyChildren({"name", "type", "default"});
        definition.type = BuildVariableType::Boolean;
        if (defaultText == "true") {
          definition.defaultValue = true;
        } else if (defaultText == "false") {
          definition.defaultValue = false;
        } else {
          parameterError(
              resource, "Param '" + definition.name +
                            "' has Boolean default '" + defaultText +
                            "'; expected true or false");
        }
      } else {
        parameterError(
            resource, "Step build variable '" + definition.name + "' has unknown type '" +
                          authoredType + "'");
      }

      if (definition.type == BuildVariableType::Integer &&
          definition.integerMinimum > definition.integerMaximum) {
        parameterError(
            resource, "Param '" + definition.name + "' has min above max");
      }
      if (definition.type == BuildVariableType::Float &&
          definition.floatMinimum > definition.floatMaximum) {
        parameterError(
            resource, "Param '" + definition.name + "' has min above max");
      }
      if (!definition.accepts(definition.defaultValue)) {
        parameterError(
            resource, "Param '" + definition.name +
                          "' has a default outside its choices or range");
      }
      definitions.push_back(move(definition));
    } while (parameter->next());

    script->setStepVariableDefinitions(move(definitions));
  }
};

}  // namespace

LuaScriptResource::LuaScriptResource(
    string const& name, string const& namesp, string const& source,
    map<string, string> const& tags, resources::ResourceLocation* location)
    : Resource(name, namesp, "LuaScript", source, tags, location) {
}

LuaScriptResource::LuaScriptResource(
    string const& name, string const& namesp, string text)
    : Resource(name, namesp, "LuaScript", "", {}, nullptr),
      mInternalText(move(text)) {
}

void LuaScriptResource::create(
    resources::DataStreamPtr data, resources::ResourceManager* resourceManager) {
  // A leaf reads its source directly, a composite reads the named TextFile
  // dependency "Source", and a built-in uses its internal text. Manifested
  // scripts then parse their Params definition.
  mResourceManager = resourceManager;
  if (data) {
    parseData(move(data));
  } else if (hasDependentResource("Source")) {
    auto source = dynamic_pointer_cast<resources::TextFileResource>(
        getDependentResource("Source"));
    if (!source) {
      throw resources::ResourceException(
          this, "LuaScript dependency 'Source' is not a TextFile resource.");
    }
    mText = source->getText();
  } else {
    parseData({});
  }

  // Internal programmatic scripts have no ResourceLocation and no manifest
  // definition. Every manifested resource has at least the resource system's
  // synthesized empty default Definition.
  if (mwLocation) {
    parseDefinition(resourceManager);
  }
}

void LuaScriptResource::parseData(resources::DataStreamPtr data) {
  if (data) {
    mText.assign(
        reinterpret_cast<char const*>(data->getData()), data->getSize());
  } else {
    mText = mInternalText;
  }
}

void LuaScriptResource::destroy() {
  mText.clear();
  mStepVariableDefinitions.clear();
  mResourceManager = nullptr;
}

string const& LuaScriptResource::getText() const {
  return mText;
}

vector<StepVariableDefinition> const&
LuaScriptResource::getStepVariableDefinitions() const {
  return mStepVariableDefinitions;
}

void LuaScriptResource::setStepVariableDefinitions(
    vector<StepVariableDefinition> definitions) {
  mStepVariableDefinitions = move(definitions);
}

map<string, string> LuaScriptResource::collectIncludedScripts() const {
  map<string, string> scripts;
  if (!mResourceManager) {
    return scripts;
  }

  // ResourceManager creates dependencies before their parent, so every text
  // below is ready by the time the parent is handed to ScriptRuntime. Expose
  // only manifest-declared transitive LuaScript dependencies, under canonical
  // qualified names such as "World/Foo".
  for (auto const& candidate :
       mResourceManager->getResourcesByType("LuaScript")) {
    if (candidate.get() == this || !dependsOn(candidate.get())) {
      continue;
    }
    auto script = dynamic_pointer_cast<LuaScriptResource>(candidate);
    if (script) {
      scripts.insert_or_assign(candidate->getQualifiedName(), script->getText());
    }
  }
  return scripts;
}

void LuaScriptResource::loadInto(
    ScriptRuntime& runtime, string const& authoredName) const {
  runtime.load(
      authoredName, getText(), collectIncludedScripts(),
      mStepVariableDefinitions);
}

void LuaScriptResource::reloadInto(
    ScriptRuntime& runtime, string const& authoredName) const {
  runtime.reload(
      authoredName, getText(), collectIncludedScripts(),
      mStepVariableDefinitions);
}

LuaScriptResourceFactory::LuaScriptResourceFactory()
    : ResourceFactory("LuaScript") {
}

resources::Resource* LuaScriptResourceFactory::createResource(
    string const& name, string const& namesp, string const& source,
    map<string, string> const& tags, resources::ResourceLocation* location) {
  return new LuaScriptResource(name, namesp, source, tags, location);
}

void registerLuaScriptResourceType(resources::ResourceManager& resourceManager) {
  resourceManager.addResourceFactory(new LuaScriptResourceFactory);
  resourceManager.addResourceDefinitionFactory(
      new LuaScriptResourceDefinitionFactory);
  resourceManager.addResource(make_shared<LuaScriptResource>(
      defaultLayerBuildStepScriptName, "World", defaultLayerBuildStepScript));
}

}  // namespace core
}  // namespace bw
