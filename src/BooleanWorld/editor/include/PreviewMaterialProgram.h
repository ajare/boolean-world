#pragma once

#include <array>
#include <cstdint>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#pragma warning(pop)

#include <common/GameDefines.h>
#include <core/Defines.h>

namespace editor {

// Compiles and drives the game's real world_pbr material shader (copied
// verbatim into editor/resources/shaders) for the 3D preview, bypassing the
// app module's WorldRenderer3d/WorldBatch/mpp::Scene stack entirely - see
// GitHub issue #256. Vertex attribute locations are fixed by the mesh
// specification this compiles the vertex stage against.
class PreviewMaterialProgram {
 public:
  static constexpr int kPositionAttrib = 0;
  static constexpr int kNormalAttrib = 1;
  static constexpr int kTexCoordAttrib = 2;
  static constexpr int kColourAttrib = 3;

  PreviewMaterialProgram();
  ~PreviewMaterialProgram();

  PreviewMaterialProgram(PreviewMaterialProgram const&) = delete;
  PreviewMaterialProgram& operator=(PreviewMaterialProgram const&) = delete;

  // Compiles the shader and loads its texture on first call. Returns false
  // if either failed; callers should skip drawing rather than use a program
  // left partially set up.
  bool ensureReady();

  void begin(
      glm::mat4 const& viewMatrix,
      glm::mat4 const& projectionMatrix,
      glm::vec3 const& cameraPosition,
      glm::vec3 const& lightPosition,
      float globalTime);

  void setMaterial(
      uint32_t materialIndex,
      std::array<float, BW_MATERIAL_PARAMS_MAX> const& params);

  void end();

 private:
  bool mInitAttempted{};
  bool mReady{};

  uint32_t mProgram{};
  uint32_t mCameraFrameUbo{};
  uint32_t mTexture{};

  int mUniformViewDistance{-1};
  int mUniformGlobalTime{-1};
  int mUniformPixelSize{-1};
  int mUniformFarGridSize{-1};
  int mUniformPlayerPosition{-1};
  int mUniformLightPosition{-1};
  int mUniformMaterialScale{-1};
  int mUniformHexagonRadius{-1};
  int mUniformHexagonDepth{-1};
  int mUniformTileDepthVariationFactor{-1};
  int mUniformRunningBondWidthPercent{-1};
  int mUniformRunningBondOffsetPercent{-1};
  int mUniformVoronoiRoundedEdgeFactor{-1};
  int mUniformSecondaryMaterialIndex{-1};
  int mUniformUseSecondaryMaterial{-1};
  int mUniformFloorPattern{-1};
  int mUniformMaterialIndex{-1};
  int mUniformMaterialParams{-1};
  int mUniformViewPos{-1};
  int mUniformTexture{-1};

  // world.vert's @MMatrix/@MCPMatrix/@NormalMatrix - every preview vertex is
  // already baked into world space, so these stay the identity transform for
  // the whole session and only need setting once per begin().
  int mUniformModel{-1};
  int mUniformModelCameraProjection{-1};
  int mUniformNormalMatrix{-1};

  bool compile();
  bool loadTexture();
};

}  // namespace editor
