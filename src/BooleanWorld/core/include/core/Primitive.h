#pragma once

#include <vector>
#include <array>
#include <optional>

#include <willpower/common/Vector2.h>
#include <willpower/common/BoundingBox.h>
#include <willpower/common/Timer.h>

#include "core/VertexTransformerObject.h"
#include "core/Triangulation.h"
#include "core/Vertex.h"
#include "core/WorldUpdateData.h"
#include "core/PrimitivePropertySet.h"
#include "core/Defines.h"

namespace bw {
namespace core {
class World;
class Layer;

class BW_API Primitive : public VertexTransformerObject {
  friend class World;
  friend class Layer;

public:
  enum struct Operation {
    Union,
    Intersection,
    Difference,
    XOR
  };

  enum struct FillRule {
    NonZero,
    EvenOdd
  };

protected:
  enum struct Index {
    Current = 0,
    Original,
    Min,
    Max
  };

private:
  World* mWorld;

  uint32_t mFlags;

  double mTime;

  float mTimeUpdateDistance;

  uint32_t mMetadata;

  Operation mOperation;

  FillRule mFillRule;

  uint8_t mPriority;

  wp::Vector2 mSize;

  PrimitivePropertySet mProperties;

  mutable wp::BoundingBox mBounds;

  mutable std::optional<Triangulation> mPickingTriangulation;

  std::vector<ComplexPolygon> mVertices;

  mutable frame_number_type mFrameNumber;

protected:
  std::vector<ComplexPolygon> mPolygons;

private:
  virtual std::vector<ComplexPolygon> generateTransformedVertices(wp::Vector2* minExtent = nullptr, wp::Vector2* maxExtent = nullptr) const;

  Triangulation const& getPickingTriangulation() const;

  bool childrenModified() const override;

protected:
  Primitive(Operation operation, FillRule fillType, std::vector<ComplexPolygon> const& complexPolygons);

  virtual void generateVertices();

  virtual std::vector<ComplexPolygon> generateVerticesImpl() = 0;

  void notifyWorldChanged() const override;

  void notifyWorldPolygonsChanged() const;

  // Called after authored polygon storage has been replaced. Most Primitive
  // types need no extra work; MeshPrimitive uses it to rebuild downstream
  // LayerBuildStep output and regenerate World geometry.
  virtual void polygonsUpdated();

  // Rotates authored geometry for rotatedCopy(). MeshPrimitive overrides this
  // because its inherited flat polygons are derived from its containment tree.
  virtual void rotateAuthoredGeometry(float angle, wp::Vector2 const& origin);

  void invalidatePostTransform(bool recalculateBounds, bool notifyWorld = true) const override;

  void copyFrom(Primitive const& other);

  void setVertices(std::vector<ComplexPolygon> const& polygons);

  // MeshPrimitive persists its authoritative containment tree instead of the
  // derived flat polygon cache. Other Primitive schemas retain that payload.
  void serializePrimitive(
      std::shared_ptr<Serializer> serializer,
      SerializationWorkData& workData,
      bool includeComplexPolygons) const;

  bool deserializePrimitive(
      std::shared_ptr<Serializer> serializer,
      SerializationWorkData& workData,
      bool includeComplexPolygons);

  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

public:
  Primitive();

  Primitive(Operation operation, FillRule fillType);

  Primitive(Primitive const& other);

  Primitive& operator=(Primitive const& other);

  virtual ~Primitive() = default;

  virtual Primitive* copy() const = 0;

  // Constructs a default Primitive of the named type through the shared
  // Primitive Registry - the single factory every deserializer goes through.
  [[nodiscard]] static Primitive* instantiate(std::string const& type);

  Primitive* rotatedCopy(float angle) const;

  void _invalidate() const;

  void setId(uint32_t id) override;

  virtual std::string getType() const = 0;

  virtual std::string getName() const;

  void setFlags(uint32_t flags);

  uint32_t getFlags() const;

  bool hasFlag(uint32_t flag) const;

  double getTime() const;

  void setTimeUpdateDistance(float dist);

  float getTimeUpdateDistance() const;

  void setMetadata(uint32_t metadata);

  uint32_t getMetadata() const;

  frame_number_type getFrameNumber() const;

  void setOperation(Operation operation);

  Operation getOperation() const;

  void setFillRule(FillRule fillRule);

  FillRule getFillRule() const;

  void setPriority(uint8_t priority);

  uint8_t getPriority() const;

  virtual float getRadius() const = 0;

  void setSize(wp::Vector2 const& size);

  void setSize(float x, float y);

  wp::Vector2 const& getSize() const;

  void setProperties(PrimitivePropertySet const& properties);

  PrimitivePropertySet const& getProperties() const;

  uint32_t getNumVertices() const;

  virtual std::vector<ComplexPolygon> const& getVertices() const;

  wp::BoundingBox const& getBounds() const;

  wp::BoundingBox calculateBounds() const;

  wp::BoundingBox calculateExactBounds() const;

  void updateVertexPositions();

  Triangulation triangulate(bool calculateBounds) const;

  void updateTime(float updateTime, WorldUpdateData const& data);

  uint32_t calculateAnimationValues();
};

}  // namespace core
}  // namespace bw