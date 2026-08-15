#pragma once

#include <vector>

#include <clipper2/clipper.h>

#include "core/Platform.h"
#include "core/WorldVertexData.h"

namespace bw {
namespace core {
namespace clipper2 {
class ZCallback {
  std::vector<WorldVertexData> mWorldVertexData;

  uint32_t mNumInterpolatedVertices;

  Clipper2Lib::ClipType mClipType;

  uint32_t mFlags;

public:
  explicit ZCallback(std::vector<WorldVertexData> const& vertexWorldData, uint32_t flags);

  std::vector<WorldVertexData>& getVertexWorldData();

  uint32_t getNumInterpolatedVertices() const;

  void setClipType(Clipper2Lib::ClipType const& clipType);

  void interpolateVertex(Clipper2Lib::Point64 const& v00,
                         Clipper2Lib::Point64 const& v01,
                         Clipper2Lib::Point64 const& v10,
                         Clipper2Lib::Point64 const& v11,
                         Clipper2Lib::Point64& p);
};

}  // namespace clipper2
}  // namespace core
}  // namespace bw
