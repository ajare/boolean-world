#include "core-lua/ScriptBindings.h"

#include <format>
#include <string>
#include <tuple>

#include <core/CoreException.h>
#include <core/DefinePrefabs.h>
#include <core/Layer.h>
#include <core/Primitive.h>
#include <core/PrimitiveField.h>

#include "core-lua/RunScript.h"

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

RunScriptContext::RunScriptContext(
    RunScript const& step, LayerBuildContext& build)
    : mStep(&step), mBuild(&build) {
}

Primitive* RunScriptContext::createPrimitive(string const& type) const {
  return mStep->createPrimitive(type);
}

void RunScriptContext::placePrimitive(Primitive* primitive) const {
  mStep->placePrimitive(*mBuild, primitive);
}

void RunScriptContext::placePrefabInstance(
    PrefabView view, float x, float y, float angle) const {
  mStep->placePrefabInstance(*mBuild, view.prefab, x, y, angle);
}

DefinePrefabsView RunScriptContext::findDefinePrefabs(string const& name) const {
  auto const id = mBuild->getLayer().findStepIdByName(name);
  if (id == ~0u) {
    throw CoreException(format("No step named '{}'", name));
  }
  auto* step = dynamic_cast<DefinePrefabs*>(mBuild->getLayer().getStepById(id));
  if (!step) {
    throw CoreException(format("Step '{}' is not a DefinePrefabs step", name));
  }
  return DefinePrefabsView{step};
}

PrimitiveFieldView RunScriptContext::findPrimitiveField(string const& name) const {
  auto const id = mBuild->getLayer().findStepIdByName(name);
  if (id == ~0u) {
    throw CoreException(format("No step named '{}'", name));
  }
  auto* step = dynamic_cast<PrimitiveField*>(mBuild->getLayer().getStepById(id));
  if (!step) {
    throw CoreException(format("Step '{}' is not a PrimitiveField step", name));
  }
  return PrimitiveFieldView{step};
}

vector<PrimitiveView> RunScriptContext::getBuildPrimitives() const {
  return toPrimitiveViews(mBuild->getBuildPrimitives());
}

tuple<float, float, float, float> RunScriptContext::getExtents() const {
  auto const& extents = mBuild->getExtents();
  return make_tuple(
      extents.getPosition().x, extents.getPosition().y,
      extents.getSize().x, extents.getSize().y);
}

vector<PrimitiveView> RunScriptContext::findBuildPrimitivesOverlapping(
    float x, float y, float width, float height) const {
  return toPrimitiveViews(mBuild->findBuildPrimitivesOverlapping(
      wp::BoundingBox(x, y, width, height)));
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

      "set_transform_offset",
      [](Primitive& primitive, float x, float y) {
        primitive.setTransformOffset({x, y});
      },
      "get_transform_offset",
      [](Primitive const& primitive) {
        return make_tuple(
            primitive.getTransformOffset().x,
            primitive.getTransformOffset().y);
      },

      "set_orientation", &Primitive::setOrientation,
      "get_orientation", &Primitive::getOrientation,

      "set_follow_orbit_angle", &Primitive::setFollowOrbitAngle,
      "get_follow_orbit_angle", &Primitive::getFollowOrbitAngle,

      "set_influence_eye_origin_offset",
      [](Primitive& primitive, float x, float y) {
        primitive.setInfluenceEyeOriginOffset({x, y});
      },
      "get_influence_eye_origin_offset",
      [](Primitive const& primitive) {
        return make_tuple(
            primitive.getInfluenceEyeOriginOffset().x,
            primitive.getInfluenceEyeOriginOffset().y);
      },
      "get_influence_eye_origin_position",
      [](Primitive const& primitive) {
        auto const position = primitive.getInfluenceEyeOriginPosition();
        return make_tuple(position.x, position.y);
      },

      "set_influence_eye_angle_offset", &Primitive::setInfluenceEyeAngleOffset,
      "get_influence_eye_angle_offset", &Primitive::getInfluenceEyeAngleOffset,

      "is_static", &Primitive::isStatic,

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

      "get_transform_offset",
      [](PrimitiveView const& view) {
        return make_tuple(
            view.primitive->getTransformOffset().x,
            view.primitive->getTransformOffset().y);
      },

      "get_orientation",
      [](PrimitiveView const& view) {
        return view.primitive->getOrientation();
      },

      "get_follow_orbit_angle",
      [](PrimitiveView const& view) {
        return view.primitive->getFollowOrbitAngle();
      },

      "get_influence_eye_origin_offset",
      [](PrimitiveView const& view) {
        return make_tuple(
            view.primitive->getInfluenceEyeOriginOffset().x,
            view.primitive->getInfluenceEyeOriginOffset().y);
      },
      "get_influence_eye_origin_position",
      [](PrimitiveView const& view) {
        auto const position = view.primitive->getInfluenceEyeOriginPosition();
        return make_tuple(position.x, position.y);
      },

      "get_influence_eye_angle_offset",
      [](PrimitiveView const& view) {
        return view.primitive->getInfluenceEyeAngleOffset();
      },

      "is_static",
      [](PrimitiveView const& view) {
        return view.primitive->isStatic();
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

  lua.new_usertype<RunScriptContext>(
      "RunScriptContext", sol::no_constructor,
      "create_primitive", &RunScriptContext::createPrimitive,
      "place_primitive", &RunScriptContext::placePrimitive,
      "place_prefab_instance", &RunScriptContext::placePrefabInstance,
      "find_define_prefabs", &RunScriptContext::findDefinePrefabs,
      "find_primitive_field", &RunScriptContext::findPrimitiveField,
      "get_build_primitives",
      [](RunScriptContext const& context) {
        return sol::as_table(context.getBuildPrimitives());
      },
      "get_extents", &RunScriptContext::getExtents,
      "find_build_primitives_overlapping",
      [](RunScriptContext const& context, float x, float y, float width, float height) {
        return sol::as_table(context.findBuildPrimitivesOverlapping(
            x, y, width, height));
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
