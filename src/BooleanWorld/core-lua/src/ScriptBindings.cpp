#include "core-lua/ScriptBindings.h"

#include <format>
#include <string>
#include <tuple>

#include <core/CoreException.h>
#include <core/DefinePrefabs.h>
#include <core/Layer.h>
#include <core/MeshPrimitive.h>
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

ClosedPolygon ringFromPoints(sol::table const& points) {
  ClosedPolygon ring;
  ring.reserve(points.size());
  for (size_t index = 1; index <= points.size(); ++index) {
    sol::object value = points[index];
    if (!value.is<sol::table>()) {
      throw CoreException(format(
          "MeshPrimitive point {} must be a {{x, y}} table", index));
    }
    auto point = value.as<sol::table>();
    sol::object x = point[1];
    sol::object y = point[2];
    if (!x.is<float>() || !y.is<float>()) {
      throw CoreException(format(
          "MeshPrimitive point {} must contain numeric x and y values", index));
    }
    ring.emplace_back(wp::Vector2{x.as<float>(), y.as<float>()});
  }
  return ring;
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

ScriptMeshPrimitive::ScriptMeshPrimitive(MeshPrimitive* primitive)
    : mPrimitive(primitive), mEditing(primitive->createEditingProxy()) {
}

MeshPrimitive* ScriptMeshPrimitive::getPrimitive() const {
  return mPrimitive;
}

void ScriptMeshPrimitive::primitiveTransformChanged() {
  auto refreshed = mPrimitive->createEditingProxy();
  auto oldMappings = mEditing->getNodeMappings();
  auto newMappings = refreshed->getNodeMappings();
  if (oldMappings.size() != newMappings.size()) {
    mEditing = shared_ptr<MeshPrimitiveEditingProxy>(move(refreshed));
    return;
  }

  auto candidate = mEditing->getMesh();
  for (size_t index = 0; index < oldMappings.size(); ++index) {
    auto oldVertices = mEditing->getPolygon(oldMappings[index].polygonIndex)
                           .getOrderedVertexIndices();
    auto newVertices = refreshed->getPolygon(newMappings[index].polygonIndex)
                           .getOrderedVertexIndices();
    if (oldVertices.size() != newVertices.size()) {
      mEditing = shared_ptr<MeshPrimitiveEditingProxy>(move(refreshed));
      return;
    }
    for (size_t vertex = 0; vertex < oldVertices.size(); ++vertex) {
      candidate.moveVertexTo(
          oldVertices[vertex],
          refreshed->getVertex(newVertices[vertex]).getPosition());
    }
  }

  if (!mEditing->replaceMesh(move(candidate))) {
    mEditing = shared_ptr<MeshPrimitiveEditingProxy>(move(refreshed));
  }
}

bool ScriptMeshPrimitive::moveVertexTo(
    uint32_t vertexId, float x, float y) {
  if (mEditing->vertexIndexIterationFinished(vertexId)) return false;
  auto candidate = mEditing->getMesh();
  candidate.moveVertexTo(vertexId, {x, y});
  if (!mEditing->replaceMesh(move(candidate))) return false;
  mEditing->commitTo(*mPrimitive);
  return true;
}

bool ScriptMeshPrimitive::moveVertex(
    uint32_t vertexId, float deltaX, float deltaY) {
  if (mEditing->vertexIndexIterationFinished(vertexId)) return false;
  auto candidate = mEditing->getMesh();
  candidate.moveVertex(vertexId, {deltaX, deltaY});
  if (!mEditing->replaceMesh(move(candidate))) return false;
  mEditing->commitTo(*mPrimitive);
  return true;
}

bool ScriptMeshPrimitive::moveEdge(
    uint32_t edgeId, float deltaX, float deltaY) {
  if (mEditing->edgeIndexIterationFinished(edgeId)) return false;
  auto candidate = mEditing->getMesh();
  candidate.moveEdge(edgeId, {deltaX, deltaY});
  if (!mEditing->replaceMesh(move(candidate))) return false;
  mEditing->commitTo(*mPrimitive);
  return true;
}

bool ScriptMeshPrimitive::movePolygon(
    uint32_t polygonId, float deltaX, float deltaY) {
  if (mEditing->polygonIndexIterationFinished(polygonId)) return false;
  auto candidate = mEditing->getMesh();
  candidate.movePolygon(polygonId, {deltaX, deltaY});
  if (!mEditing->replaceMesh(move(candidate))) return false;
  mEditing->commitTo(*mPrimitive);
  return true;
}

optional<uint32_t> ScriptMeshPrimitive::splitEdge(uint32_t edgeId) {
  return splitEdge(edgeId, 0.5f);
}

optional<uint32_t> ScriptMeshPrimitive::splitEdge(
    uint32_t edgeId, float t) {
  if (mEditing->edgeIndexIterationFinished(edgeId) || t <= 0.0f || t >= 1.0f) {
    return nullopt;
  }
  wp::geometry::SplitEdgeResult result;
  if (!mEditing->splitEdge(edgeId, t, &result) ||
      result.newVertexIndices.empty()) {
    return nullopt;
  }
  mEditing->commitTo(*mPrimitive);
  return result.newVertexIndices.front();
}

bool ScriptMeshPrimitive::removeVertex(uint32_t vertexId) {
  if (mEditing->vertexIndexIterationFinished(vertexId) ||
      !mEditing->removeVertex(vertexId)) {
    return false;
  }
  mEditing->commitTo(*mPrimitive);
  return true;
}

bool ScriptMeshPrimitive::removeEdge(uint32_t edgeId) {
  if (mEditing->edgeIndexIterationFinished(edgeId) ||
      !mEditing->removeEdge(edgeId)) {
    return false;
  }
  mEditing->commitTo(*mPrimitive);
  return true;
}

bool ScriptMeshPrimitive::removePolygon(uint32_t polygonId) {
  if (mEditing->polygonIndexIterationFinished(polygonId) ||
      !mEditing->removeRing(polygonId)) {
    return false;
  }
  mEditing->commitTo(*mPrimitive);
  return true;
}

optional<uint32_t> ScriptMeshPrimitive::addShell(sol::table const& points) {
  auto id = mEditing->addShell(ringFromPoints(points));
  mEditing->commitTo(*mPrimitive);
  return id;
}

optional<uint32_t> ScriptMeshPrimitive::addHole(
    uint32_t filledPolygonId, sol::table const& points) {
  auto id = mEditing->addHole(filledPolygonId, ringFromPoints(points));
  if (id == ~0u) return nullopt;
  mEditing->commitTo(*mPrimitive);
  return id;
}

optional<uint32_t> ScriptMeshPrimitive::addIsland(
    uint32_t holePolygonId, sol::table const& points) {
  auto id = mEditing->addIsland(holePolygonId, ringFromPoints(points));
  if (id == ~0u) return nullopt;
  mEditing->commitTo(*mPrimitive);
  return id;
}

optional<uint32_t> ScriptMeshPrimitive::fillHole(uint32_t holePolygonId) {
  auto id = mEditing->fillHole(holePolygonId);
  if (id == ~0u) return nullopt;
  mEditing->commitTo(*mPrimitive);
  return id;
}

bool ScriptMeshPrimitive::slicePolygon(
    uint32_t polygonId, uint32_t firstVertexId,
    uint32_t secondVertexId) {
  if (!mEditing->sliceFilledRing(
          polygonId, firstVertexId, secondVertexId)) {
    return false;
  }
  mEditing->commitTo(*mPrimitive);
  return true;
}

ScriptMeshPrimitive RunScriptContext::createMeshPrimitive(sol::table const& points) const {
  auto ring = ringFromPoints(points);
  auto primitive = unique_ptr<MeshPrimitive>(MeshPrimitive::fromComplexPolygons(
      Primitive::Operation::Union, {{move(ring)}}));
  auto* borrowed = primitive.get();
  (void)mStep->ownPrimitive(move(primitive));
  return ScriptMeshPrimitive(borrowed);
}

void RunScriptContext::placePrimitive(Primitive* primitive) const {
  mStep->placePrimitive(*mBuild, primitive);
}

void RunScriptContext::placeMeshPrimitive(
    ScriptMeshPrimitive const& primitive) const {
  placePrimitive(primitive.getPrimitive());
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

  lua.new_usertype<ScriptMeshPrimitive>(
      "MeshPrimitive", sol::no_constructor,

      "get_type",
      [](ScriptMeshPrimitive const& mesh) {
        return mesh.getPrimitive()->getType();
      },

      "set_position",
      [](ScriptMeshPrimitive& mesh, float x, float y) {
        mesh.getPrimitive()->setPosition({x, y});
        mesh.primitiveTransformChanged();
      },
      "get_position",
      [](ScriptMeshPrimitive const& mesh) {
        auto const& position = mesh.getPrimitive()->getPosition();
        return make_tuple(position.x, position.y);
      },

      "set_transform_offset",
      [](ScriptMeshPrimitive& mesh, float x, float y) {
        mesh.getPrimitive()->setTransformOffset({x, y});
        mesh.primitiveTransformChanged();
      },
      "get_transform_offset",
      [](ScriptMeshPrimitive const& mesh) {
        auto const& offset = mesh.getPrimitive()->getTransformOffset();
        return make_tuple(offset.x, offset.y);
      },

      "set_orientation",
      [](ScriptMeshPrimitive& mesh, float angle) {
        mesh.getPrimitive()->setOrientation(angle);
        mesh.primitiveTransformChanged();
      },
      "get_orientation",
      [](ScriptMeshPrimitive const& mesh) {
        return mesh.getPrimitive()->getOrientation();
      },

      "set_follow_orbit_angle",
      [](ScriptMeshPrimitive& mesh, bool follow) {
        mesh.getPrimitive()->setFollowOrbitAngle(follow);
        mesh.primitiveTransformChanged();
      },
      "get_follow_orbit_angle",
      [](ScriptMeshPrimitive const& mesh) {
        return mesh.getPrimitive()->getFollowOrbitAngle();
      },

      "set_influence_eye_origin_offset",
      [](ScriptMeshPrimitive& mesh, float x, float y) {
        mesh.getPrimitive()->setInfluenceEyeOriginOffset({x, y});
        mesh.primitiveTransformChanged();
      },
      "get_influence_eye_origin_offset",
      [](ScriptMeshPrimitive const& mesh) {
        auto const& offset = mesh.getPrimitive()->getInfluenceEyeOriginOffset();
        return make_tuple(offset.x, offset.y);
      },
      "get_influence_eye_origin_position",
      [](ScriptMeshPrimitive const& mesh) {
        auto const position = mesh.getPrimitive()->getInfluenceEyeOriginPosition();
        return make_tuple(position.x, position.y);
      },

      "set_influence_eye_angle_offset",
      [](ScriptMeshPrimitive& mesh, float angle) {
        mesh.getPrimitive()->setInfluenceEyeAngleOffset(angle);
        mesh.primitiveTransformChanged();
      },
      "get_influence_eye_angle_offset",
      [](ScriptMeshPrimitive const& mesh) {
        return mesh.getPrimitive()->getInfluenceEyeAngleOffset();
      },

      "is_static",
      [](ScriptMeshPrimitive const& mesh) {
        return mesh.getPrimitive()->isStatic();
      },

      "set_size",
      [](ScriptMeshPrimitive& mesh, float x, float y) {
        mesh.getPrimitive()->setSize(x, y);
        mesh.primitiveTransformChanged();
      },
      "get_size",
      [](ScriptMeshPrimitive const& mesh) {
        auto const& size = mesh.getPrimitive()->getSize();
        return make_tuple(size.x, size.y);
      },

      "set_priority",
      [](ScriptMeshPrimitive& mesh, uint32_t priority) {
        mesh.getPrimitive()->setPriority(static_cast<uint8_t>(priority));
      },
      "get_priority",
      [](ScriptMeshPrimitive const& mesh) {
        return static_cast<uint32_t>(mesh.getPrimitive()->getPriority());
      },

      "set_operation",
      [](ScriptMeshPrimitive& mesh, string const& operation) {
        mesh.getPrimitive()->setOperation(operationFromName(operation));
      },
      "get_operation",
      [](ScriptMeshPrimitive const& mesh) {
        return operationName(mesh.getPrimitive()->getOperation());
      },

      "split_edge",
      sol::overload(
          [](ScriptMeshPrimitive& mesh, uint32_t edgeId) {
            return mesh.splitEdge(edgeId);
          },
          [](ScriptMeshPrimitive& mesh, uint32_t edgeId, float t) {
            return mesh.splitEdge(edgeId, t);
          }),
      "move_vertex_to", &ScriptMeshPrimitive::moveVertexTo,
      "move_vertex", &ScriptMeshPrimitive::moveVertex,
      "move_edge", &ScriptMeshPrimitive::moveEdge,
      "move_polygon", &ScriptMeshPrimitive::movePolygon,
      "remove_vertex", &ScriptMeshPrimitive::removeVertex,
      "remove_edge", &ScriptMeshPrimitive::removeEdge,
      "remove_polygon", &ScriptMeshPrimitive::removePolygon,
      "add_shell", &ScriptMeshPrimitive::addShell,
      "add_hole", &ScriptMeshPrimitive::addHole,
      "add_island", &ScriptMeshPrimitive::addIsland,
      "fill_hole", &ScriptMeshPrimitive::fillHole,
      "slice_polygon", &ScriptMeshPrimitive::slicePolygon);

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
      "create_mesh_primitive", &RunScriptContext::createMeshPrimitive,
      "place_primitive",
      sol::overload(
          &RunScriptContext::placePrimitive,
          &RunScriptContext::placeMeshPrimitive),
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
