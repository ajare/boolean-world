#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <willpower/common/Vector2.h>
#include <willpower/common/BoundingBox.h>

#include "Platform.h"
#include "core/Defines.h"
#include "core/WallMaskOverride.h"
#include "core/WallNormalMapOverride.h"

namespace bw {
namespace core {

struct Vertex {
  wp::Vector2 p;

  // Authored script-facing data attached to this topology vertex. Mesh editing
  // keeps this identical across every Ring occurrence of a welded vertex.
  std::map<std::string, std::string> metadata;

  // Authored script-facing data attached to the edge FROM this vertex TO THE
  // NEXT vertex in its Ring. Mesh editing keeps this identical across every
  // Ring occurrence of a welded edge.
  std::map<std::string, std::string> edgeMetadata;

  // Flags for the edge FROM this vertex TO THE NEXT vertex in its Ring (the
  // "outgoing edge"). See the BW_MESH_EDGE_* flags in Defines.h. All bits
  // clear means collision is not overridden and the wall is visible.
  uint32_t edgeFlags{0};
  WallNormalMapOverride edgeNormalMap{};
  WallMaskOverride edgeWallMask{};

  Vertex() = default;
  Vertex(wp::Vector2 const& position)
      : p(position) {
  }
};

typedef std::vector<Vertex> ClosedPolygon;

typedef std::vector<Vertex> OpenPolygon;

typedef std::vector<Vertex> VertexList;

typedef std::vector<ClosedPolygon> ComplexPolygon;

wp::BoundingBox calculatePolygonBounds(ClosedPolygon const& polygon);

wp::BoundingBox calculatePolygonBounds(ComplexPolygon const& polygon);

wp::BoundingBox calculatePolygonBounds(std::vector<ComplexPolygon> const& polygons);

}  // namespace core
}  // namespace bw
