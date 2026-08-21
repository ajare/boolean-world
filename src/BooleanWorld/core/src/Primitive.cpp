#include <array>
#include <cassert>

#include <willpower/common/MathsUtils.h>

#include "core/Primitive.h"
#include "core/CirclePolygon.h"
#include "core/CircleSegmentPolygon.h"
#include "core/CoreException.h"
#include "core/MeshPrimitive.h"
#include "core/RectanglePolygon.h"
#include "core/RegularPolygon.h"
#include "core/Registry.h"
#include "core/SuperformulaPolygon.h"
#include "core/TorusPolygon.h"
#include "core/TorusSegmentPolygon.h"
#include "core/Triangulator.h"
#include "core/World.h"
#include "core/Utils.h"

namespace bw {
namespace core {
using namespace std;

Primitive* Primitive::instantiate(string const& type) {
  static const Registry<Primitive> primitiveRegistry(
      "primitive", {{"Rectangle", []() { return new RectanglePolygon; }},
                    {"Regular", []() { return new RegularPolygon; }},
                    {"Circle", []() { return new CirclePolygon; }},
                    {"CircleSegment", []() { return new CircleSegmentPolygon; }},
                    {"Torus", []() { return new TorusPolygon; }},
                    {"TorusSegment", []() { return new TorusSegmentPolygon; }},
                    {"Superformula", []() { return new SuperformulaPolygon; }},
                    {"Mesh", []() { return new MeshPrimitive; }}});

  return primitiveRegistry.create(type);
}

Primitive::Primitive()
    : Primitive(Operation::Union, FillRule::NonZero) {
}

Primitive::Primitive(Operation operation, FillRule fillType)
    : Primitive(operation, fillType, {}) {
}

Primitive::Primitive(Operation operation, FillRule fillType, vector<ComplexPolygon> const& complexPolygons)
    : mWorld(nullptr), mFlags(BW_PRIMITIVE_INTERACTS_FLAG), mTime(0.0), mTimeUpdateDistance(numeric_limits<float>::max()), mMetadata(0), mOperation(operation), mFillRule(fillType), mPriority(0), mSize(100.0f, 100.0f), mProperties{}, mFrameNumber(0), mPolygons(complexPolygons) {
}

Primitive::Primitive(Primitive const& other) {
  copyFrom(other);
}

Primitive& Primitive::operator=(Primitive const& other) {
  copyFrom(other);
  return *this;
}

void Primitive::copyFrom(Primitive const& other) {
  VertexTransformerObject::copyFrom(other);

  // A copy starts unregistered even when `other` is live in a World: it is
  // not yet present in any Layer's Primitive list, so notifying that World
  // of a mutation (e.g. the position/parent writes in
  // PrefabField::execute()) would fail World::primitiveChanged's lookup.
  // World::addPrimitive (or Layer::rebindOwnedState) binds mWorld once the
  // copy actually joins a Layer.
  mFlags = other.mFlags;
  mTime = 0.0f;
  mTimeUpdateDistance = other.mTimeUpdateDistance;
  mMetadata = other.mMetadata;
  mOperation = other.mOperation;
  mFillRule = other.mFillRule;
  mPriority = other.mPriority;
  mSize = other.mSize;
  mProperties = other.mProperties;
  mBounds = other.mBounds;
  mPickingTriangulation.reset();
  mVertices = other.mVertices;
  mPolygons = other.mPolygons;
  mFrameNumber = other.mFrameNumber;
}

Primitive* Primitive::rotatedCopy(float angle) const {
  auto p = copy();

  auto const& origin = getTransformOffset();

  // Rotate position
  p->setPosition(getPosition().rotatedClockwiseCopy(angle));

  // Rotate authored geometry. Procedural Primitives rotate their polygon
  // parameters; MeshPrimitive rotates its authoritative containment tree.
  p->rotateAuthoredGeometry(angle, origin);

  // Bump angles
  p->setInfluenceEyeAngleOffset(p->getInfluenceEyeAngleOffset() + angle);

  return p;
}

void Primitive::invalidatePostTransform(bool recalculateBounds, bool notifyWorld) const {
  if (recalculateBounds) {
    mBounds = calculateBounds();
  }

  if (mWorld) {
    mFrameNumber = mWorld->getFrameNumber();
  }

  if (notifyWorld) {
    notifyWorldChanged();
  }

  _invalidate();
}

void Primitive::_invalidate() const {
  mPickingTriangulation.reset();
}

void Primitive::notifyWorldChanged() const {
  if (mWorld) {
    mWorld->primitiveChanged(this);
  }
}

void Primitive::notifyWorldPolygonsChanged() const {
  if (mWorld) {
    mWorld->primitivePolygonsChanged(this);
  }
}

void Primitive::polygonsUpdated() {
}

void Primitive::rotateAuthoredGeometry(float angle, wp::Vector2 const& origin) {
  auto polygons = mPolygons;
  for (auto& complexPolygon : polygons) {
    for (auto& polygon : complexPolygon) {
      for (auto& vertex : polygon) {
        vertex.p -= origin;
        vertex.p.rotateClockwise(angle);
        vertex.p += origin;
      }
    }
  }
  setVertices(polygons);
}

void Primitive::setId(uint32_t id) {
  VertexTransformerObject::setId(id);
}

string Primitive::getName() const {
  if (hasFlag(BW_PRIMITIVE_GHOST_FLAG)) {
    return "Ghost";
  } else {
    return getType();
  }
}

void Primitive::setFlags(uint32_t flags) {
  mFlags = flags;
}

uint32_t Primitive::getFlags() const {
  return mFlags;
}

bool Primitive::hasFlag(uint32_t flag) const {
  return (mFlags & flag) != 0;
}

void Primitive::setMetadata(uint32_t metadata) {
  mMetadata = metadata;
}

uint32_t Primitive::getMetadata() const {
  return mMetadata;
}

double Primitive::getTime() const {
  return mTime;
}

void Primitive::setTimeUpdateDistance(float dist) {
  mTimeUpdateDistance = dist;
}

float Primitive::getTimeUpdateDistance() const {
  return mTimeUpdateDistance;
}

frame_number_type Primitive::getFrameNumber() const {
  return mFrameNumber;
}

void Primitive::setOperation(Operation operation) {
  mOperation = operation;
  notifyWorldChanged();
}

Primitive::Operation Primitive::getOperation() const {
  return mOperation;
}

void Primitive::setFillRule(FillRule fillRule) {
  mFillRule = fillRule;
  notifyWorldChanged();
}

Primitive::FillRule Primitive::getFillRule() const {
  return mFillRule;
}

void Primitive::setPriority(uint8_t priority) {
  mPriority = priority;
  notifyWorldChanged();
}

uint8_t Primitive::getPriority() const {
  return mPriority;
}

void Primitive::setSize(wp::Vector2 const& size) {
  mSize = size;
  notifyWorldChanged();
}

void Primitive::setSize(float x, float y) {
  mSize.set(x, y);
  notifyWorldChanged();
}

wp::Vector2 const& Primitive::getSize() const {
  return mSize;
}

void Primitive::setProperties(PrimitivePropertySet const& properties) {
  mProperties = properties;
  notifyWorldChanged();
}

PrimitivePropertySet const& Primitive::getProperties() const {
  return mProperties;
}

uint32_t Primitive::getNumVertices() const {
  uint32_t numVertices{0};

  for (auto const& polygon : mPolygons) {
    for (auto loop : polygon) {
      numVertices += (uint32_t)loop.size();
    }
  }

  return numVertices;
}

void Primitive::setVertices(vector<ComplexPolygon> const& polygons) {
  for (auto const& polygon : polygons) {
    for (auto const& path : polygon) {
      if (path.size() > BW_WORLD_PRIMITIVE_VERTEX_COUNT_MAX) {
        throw CoreException("Too many vertices in Primitive sub-polygon!");
      }
    }
  }

  mPolygons = polygons;
  updateVertexPositions();
  invalidatePostTransform(true, true);
  polygonsUpdated();
}

bool Primitive::childrenModified() const {
  return VertexTransformerObject::childrenModified() || mProperties.isModified();
}

void Primitive::serializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  VertexTransformerObject::serializeImpl(serializer, workData);

  serializer->beginMap("primitive");
  {
    serializer->writeUint32("flags", mFlags);
    serializer->writeFloat("timeUpdateDistance", mTimeUpdateDistance);
    serializer->writeUint32("metadata", mMetadata);
    serializer->writeUint32("operation", (uint32_t)mOperation);
    serializer->writeUint32("fillRule", (uint32_t)mFillRule);
    serializer->writeInt32("priority", (int32_t)mPriority);
    serializer->writeVector2("size", mSize);

    mProperties.serialize(serializer, workData);

    serializer->beginArray("complexPolygons");
    {
      for (auto const& complexPolygon : mPolygons) {
        serializer->beginMap("complexPolygon");
        {
          serializer->beginArray("polygons");
          {
            for (auto const& polygon : complexPolygon) {
              serializer->beginMap("polygon");
              {
                serializer->beginArray("vertices");
                {
                  for (auto const& vertex : polygon) {
                    serializer->beginMap("vertex");
                    {
                      serializer->writeVector2("p", vertex.p);

                      serializer->endMap();  // vertex
                    }
                  }

                  serializer->endArray();  // vertices
                }

                serializer->endMap();  // polygon
              }
            }

            serializer->endArray();  // polygons
          }

          serializer->endMap();  // complexPolygon
        }
      }

      serializer->endArray();  // complexPolygons
    }

    serializer->endMap();  // primitive
  }
}

bool Primitive::deserializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  if (!VertexTransformerObject::deserializeImpl(serializer, workData)) {
    return false;
  }

  uint32_t flags;
  float timeUpdateDistance;
  uint32_t metadata;
  Operation operation;
  FillRule fillRule;
  uint8_t priority;
  wp::Vector2 size;
  PrimitivePropertySet properties;
  vector<ComplexPolygon> complexPolygons;

  try {
    serializer->beginMap("primitive");
    {
      flags = serializer->readUint32("flags");
      timeUpdateDistance = serializer->readFloat("timeUpdateDistance");
      metadata = serializer->readUint32("metadata", true);
      operation = (Operation)serializer->readUint32("operation");
      fillRule = (FillRule)serializer->readUint32("fillRule");
      priority = (uint8_t)serializer->readInt32("priority");
      size = serializer->readVector2("size");

      if (!properties.deserialize(serializer, workData)) {
        copyErrorsAndWarnings(&properties, true, true);
        return false;
      }

      serializer->beginArray("complexPolygons");
      {
        while (serializer->nextArrayItem()) {
          serializer->beginMap("complexPolygon");
          {
            ComplexPolygon complexPolygon;

            serializer->beginArray("polygons");
            {
              while (serializer->nextArrayItem()) {
                serializer->beginMap("polygon");
                {
                  ClosedPolygon polygon;

                  serializer->beginArray("vertices");
                  {
                    while (serializer->nextArrayItem()) {
                      serializer->beginMap("vertex");
                      {
                        wp::Vector2 p = serializer->readVector2("p");

                        // Retain this ignored legacy field so shipped pre-arrangement
                        // world files remain loadable after the Clipper z-bitfield removal.
                        // Positional formats (e.g. binary) never had this field to begin
                        // with, so there is nothing to skip there.
                        if (!serializer->isPositional()) {
                          serializer->readInt64("z", true, 0);
                        }

                        polygon.push_back({p});

                        serializer->endMap();  // vertex
                      }
                    }

                    serializer->endArray();  // vertices
                  }

                  serializer->endMap();  // polygon

                  complexPolygon.push_back(polygon);
                }
              }

              serializer->endArray();  // polygons
            }

            serializer->endMap();  // complexPolygon

            complexPolygons.push_back(complexPolygon);
          }
        }

        serializer->endArray();  // complexPolygons
      }

      serializer->endMap();  // primitive
    }
  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  // Commit
  mFlags = flags;
  mTime = 0.0;
  mTimeUpdateDistance = timeUpdateDistance;
  mMetadata = metadata;
  mOperation = operation;
  mFillRule = fillRule;
  mPriority = priority;
  mSize = size;
  mFrameNumber = 0;
  mProperties = properties;
  mPolygons = complexPolygons;

  _invalidate();

  return true;
}

void Primitive::generateVertices() {
  setVertices(generateVerticesImpl());
}

vector<ComplexPolygon> Primitive::generateTransformedVertices(wp::Vector2* minExtent, wp::Vector2* maxExtent) const {
  if (minExtent) {
    minExtent->set(1e10f, 1e10f);
  }
  if (maxExtent) {
    maxExtent->set(-1e10f, -1e10f);
  }

  vector<ComplexPolygon> transformedVertices;
  bool verticesChanged{false};

  for (auto const& complexPolygon : mPolygons) {
    ComplexPolygon polyVertices;

    for (auto const& polygon : complexPolygon) {
      auto numVertices = (uint32_t)polygon.size();
      ClosedPolygon vertices(numVertices);

      for (uint32_t i = 0; i < numVertices; ++i) {
        bool vChanged{false};

        vertices[i].p = transformVertex(polygon[i].p * mSize, &vChanged);
        verticesChanged = verticesChanged || vChanged;

        if (minExtent) {
          if (vertices[i].p.x < minExtent->x) {
            minExtent->x = vertices[i].p.x;
          }
          if (vertices[i].p.y < minExtent->y) {
            minExtent->y = vertices[i].p.y;
          }
        }
        if (maxExtent) {
          if (vertices[i].p.x > maxExtent->x) {
            maxExtent->x = vertices[i].p.x;
          }
          if (vertices[i].p.y > maxExtent->y) {
            maxExtent->y = vertices[i].p.y;
          }
        }
      }

      polyVertices.push_back(vertices);
    }

    transformedVertices.push_back(polyVertices);
  }

  if (verticesChanged) {
    invalidatePostTransform(false, false);
  }

  return transformedVertices;
}

vector<ComplexPolygon> const& Primitive::getVertices() const {
  return mVertices;
}

wp::BoundingBox Primitive::calculateExactBounds() const {
  return calculatePolygonBounds(generateTransformedVertices());
}

wp::BoundingBox Primitive::calculateBounds() const {
  if (hasFlag(BW_PRIMITIVE_EXACT_BOUNDS_FLAG)) {
    return calculateExactBounds();
  } else {
    auto position = calculateWorldPosition();

    // Get radius
    float radius = getRadius();

    // Multiply by max scale * size
    float minScale, maxScale, minOrbitDist, maxOrbitDist;

    auto const& scaleLerper = getAnimationInterpolator(VertexTransformer::Key::Scale);
    scaleLerper.getValueExtents(&minScale, &maxScale);

    // Get max orbit distance
    auto const& orbitDistLerper = getAnimationInterpolator(VertexTransformer::Key::OrbitDistance);
    orbitDistLerper.getValueExtents(&minOrbitDist, &maxOrbitDist);

    // Get size
    wp::Vector2 extent = mSize * 0.5f * maxScale * radius + maxOrbitDist;
    return wp::BoundingBox(position - extent, extent * 2.0f);
  }
}

void Primitive::updateVertexPositions() {
  mVertices = generateTransformedVertices();
  mPickingTriangulation.reset();
}

wp::BoundingBox const& Primitive::getBounds() const {
  return mBounds;
}

Triangulation const& Primitive::getPickingTriangulation() const {
  if (!mPickingTriangulation) {
    mPickingTriangulation = triangulate(true);
  }
  return *mPickingTriangulation;
}

Triangulation Primitive::triangulate(bool calculateBounds) const {
  Triangulation result;

  auto const& complexPolygons = getVertices();

  for (auto const& complexPolygon : complexPolygons) {
    Triangulator triangulator(nullptr, calculateBounds, false, false);
    vector<TriangulationData> triangulationData;
    ClosedPolygon vertices;

    for (auto const& polygon : complexPolygon) {
      TriangulationData td;

      td.triangulation = polygon;
      td.primitiveIndex = getId();

      std::copy(polygon.begin(), polygon.end(), back_inserter(vertices));

      triangulationData.push_back(td);
    }

    triangulator._triangulate(triangulationData, vertices);
    result.merge(triangulator.getTriangulation(), calculateBounds);
  }

  return result;
}

void Primitive::updateTime(float updateTime, WorldUpdateData const& data) {
  // Only update time if the conditions are met
  if (getPosition().distanceTo(data.entityPosition) > mTimeUpdateDistance) {
    return;
  }

  if ((mFlags & BW_PRIMITIVE_NO_TIME_UPDATE_PLAYER_STATIC) && !(data.entityMoved || data.entityTurned)) {
    return;
  }

  if (mFlags & BW_PRIMITIVE_NO_TIME_UPDATE_IF_VISIBLE) {
    // Simple, cone based visibility
    auto const& bounds = getBounds();

    wp::Vector2 minBounds, maxBounds;

    bounds.getExtents(minBounds, maxBounds);

    auto const v0 = data.entityPosition;
    auto const [v1, v2] = calculateFovTriangle(v0, data.entityAngle, data.entityViewDist, data.entityFov);

    if (wp::MathsUtils::boxIntersectsTriangle(minBounds, maxBounds, v0, v1, v2)) {
      return;
    }
  }

  mTime += updateTime;

  if (updateTime != 0) {
    _invalidate();
  }
}

uint32_t Primitive::calculateAnimationValues() {
  return VertexTransformerObject::calculateAnimationValues(mTime);
}

}  // namespace core
}  // namespace bw