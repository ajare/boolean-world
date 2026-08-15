#pragma once

#include <vector>

#include <clipper2/clipper.h>

#include <willpower/common/Vector2.h>
#include <willpower/common/BoundingBox.h>
#include <willpower/common/Timer.h>

#include "core/Platform.h"
#include "core/Primitive.h"
#include "core/WorldVertexData.h"
#include "core/Clipper2Polygon.h"
#include "core/ClippedPolygon.h"
#include "core/ZCallback.h"
#include "core/Stats.h"
#include "core/PolygonGraph.h"

namespace bw {
namespace core {
class World;

class BW_API Clipper {
  std::vector<WorldVertexData> mBaseWorldVertexData;

  uint32_t mFlags;

  std::vector<Clipper2Polygon> mBorder;

  graph::PolygonGraph mArrangementGraph;

  World const* mwWorld;

protected:
  struct ClipData {
    Clipper2Lib::Paths64 paths;
    Clipper2Lib::ClipType ct;
    Clipper2Lib::FillRule fr;
    bool saveBeforeClip{false};
  };

protected:
  std::vector<WorldVertexData> mClippedWorldVertexData;

  clipper2::ZCallback mCallback;

  ClipStats mStats;

private:
  void setPolygonPrimitiveIndex(Clipper2Lib::Paths64& polygons, uint32_t primitiveIndex);

  void setPolygonPrimitiveIndex(std::vector<Clipper2Polygon>& polygons, uint32_t primitiveIndex);

  void addIntermediateClipping(Clipper2Lib::Paths64 const& paths, uint32_t primitiveIndex, std::vector<Clipper2Lib::Paths64>& states);

  std::vector<Clipper2Lib::Paths64> generateIntermediateClippings(std::vector<ClipData> const& paths);

  std::vector<Clipper2Polygon> clipIntermediateClippings(std::vector<Clipper2Lib::Paths64> const& states, Clipper2Lib::ClipType clipType, bool interpolate);

  std::vector<Clipper2Polygon> clipIntermediateClipping(Clipper2Lib::Paths64 const& state, std::vector<Clipper2Polygon> const& polygons, Clipper2Lib::ClipType clipType, uint32_t primitiveIndex);

  void calculateCombinedPolygons(Clipper2Lib::Paths64 const& interState, std::vector<Clipper2Polygon> const& arrangePolygons, Clipper2Lib::ClipType clipType, uint32_t primitiveIndex, std::vector<Clipper2Lib::Paths64>& combinedPaths);

  void buildPolygonGraph(std::vector<Clipper2Polygon> const& polygons);

protected:
  std::vector<Clipper2Polygon> executeClip(Clipper2Lib::Clipper64& clipper, Clipper2Lib::ClipType op, Clipper2Lib::FillRule fillRule, PrimitivePropertySet const& clipProperties);

  std::vector<Clipper2Polygon> clip(std::vector<ClipData> const& paths);

public:
  Clipper(std::vector<WorldVertexData> const& baseWorldVertexData, std::vector<Clipper2Lib::Paths64> const& intermediateStates, World const* world, uint32_t flags = 0);

  std::vector<WorldVertexData> const& getBaseWorldVertexData() const;

  std::vector<WorldVertexData> const& getClippedWorldVertexData() const;

  std::vector<Clipper2Polygon> const& getBorderPolygons() const;

  graph::PolygonGraph const& getArrangementGraph() const;

  ClipStats getStats() const;

  std::vector<Clipper2Polygon> clipToClipper2Polygons(std::vector<Primitive*> const& primitives, Primitive::Operation unionReplacementOp = Primitive::Operation::Union, wp::BoundingBox const* bounds = nullptr);

  std::vector<ClippedPolygon> clipToClippedPolygons(std::vector<Primitive*> const& primitives, Primitive::Operation unionReplacementOp = Primitive::Operation::Union, wp::BoundingBox const* bounds = nullptr);
};

}  // namespace core
}  // namespace bw
