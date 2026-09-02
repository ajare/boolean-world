#include "core-lua/ScriptBindings.h"

#include <format>
#include <string>
#include <tuple>

#include <core/CoreException.h>
#include <core/DefinePrefabs.h>
#include <core/Primitive.h>
#include <core/PrimitiveField.h>

namespace bw {
namespace core {

using namespace std;

namespace {

constexpr char const* boundMarker = "__bw_script_types_bound";

string operationName(Primitive::Operation operation) {
  switch (operation) {
    case Primitive::Operation::Union: return "union";
    case Primitive::Operation::Intersection: return "intersection";
    case Primitive::Operation::Difference: return "difference";
    case Primitive::Operation::XOR: return "xor";
  }
  throw CoreException("Unknown Primitive operation");
}

Primitive::Operation operationFromName(string const& name) {
  if (name == "union") return Primitive::Operation::Union;
  if (name == "intersection") return Primitive::Operation::Intersection;
  if (name == "difference") return Primitive::Operation::Difference;
  if (name == "xor") return Primitive::Operation::XOR;
  throw CoreException(format("'{}' is not a Primitive operation", name));
}

}  // namespace

vector<PrimitiveView> toPrimitiveViews(vector<Primitive*> const& primitives) {
  vector<PrimitiveView> views;
  views.reserve(primitives.size());
  for (auto* primitive : primitives) {
    views.push_back(PrimitiveView{primitive});
  }
  return views;
}

void bindScriptTypes(sol::state& lua) {
  if (lua[boundMarker].valid()) {
    return;
  }

  lua.new_usertype<Primitive>(
      "Primitive", sol::no_constructor,

      "get_type", &Primitive::getType,

      "set_position",
      [](Primitive& primitive, float x, float y) {
        primitive.setPosition({x, y});
      },
      "get_position",
      [](Primitive const& primitive) {
        return make_tuple(primitive.getPosition().x, primitive.getPosition().y);
      },

      "set_size",
      [](Primitive& primitive, float x, float y) { primitive.setSize(x, y); },
      "get_size",
      [](Primitive const& primitive) {
        return make_tuple(primitive.getSize().x, primitive.getSize().y);
      },

      "set_priority",
      [](Primitive& primitive, uint32_t priority) {
        primitive.setPriority(static_cast<uint8_t>(priority));
      },
      "get_priority",
      [](Primitive const& primitive) {
        return static_cast<uint32_t>(primitive.getPriority());
      },

      "set_operation",
      [](Primitive& primitive, string const& operation) {
        primitive.setOperation(operationFromName(operation));
      },
      "get_operation",
      [](Primitive const& primitive) {
        return operationName(primitive.getOperation());
      });

  lua.new_usertype<PrimitiveView>(
      "PrimitiveView", sol::no_constructor,

      "get_type", [](PrimitiveView const& view) { return view.primitive->getType(); },

      "get_position",
      [](PrimitiveView const& view) {
        return make_tuple(view.primitive->getPosition().x, view.primitive->getPosition().y);
      },

      "get_size",
      [](PrimitiveView const& view) {
        return make_tuple(view.primitive->getSize().x, view.primitive->getSize().y);
      },

      "get_priority",
      [](PrimitiveView const& view) {
        return static_cast<uint32_t>(view.primitive->getPriority());
      },

      "get_operation",
      [](PrimitiveView const& view) {
        return operationName(view.primitive->getOperation());
      });

  lua.new_usertype<PrefabView>(
      "Prefab", sol::no_constructor,

      "get_name", [](PrefabView const& view) { return view.prefab->getName(); });

  lua.new_usertype<DefinePrefabsView>(
      "DefinePrefabsStep", sol::no_constructor,

      "get_prefab",
      [](DefinePrefabsView const& view, string const& name) {
        auto const id = view.step->findPrefabIdByName(name);
        if (id == ~0u) {
          throw CoreException(format("No Prefab named '{}'", name));
        }
        return PrefabView{view.step->findPrefabById(id)};
      });

  lua.new_usertype<PrimitiveFieldView>(
      "PrimitiveFieldStep", sol::no_constructor,

      "get_primitives",
      [](PrimitiveFieldView const& view) {
        return sol::as_table(toPrimitiveViews(view.step->getPrimitives()));
      });

  lua[boundMarker] = true;
}

}  // namespace core
}  // namespace bw
