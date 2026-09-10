#pragma once

#include <functional>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

#include <mpp/TriangleBatch.h>
#include <mpp/ProgrammaticModelStream.h>

#include <core/World.h>
#include <core/MaterialDefinition.h>

#include "SurfaceMaterialResolver.h"
#include "WallRenderVariant.h"

enum class WorldSurfaceSet {
  Horizontal,
  Liquid,
  Walls,
};

class WorldBatch : public mpp::TriangleBatch {
  bw::core::World const* mWorld;
  WorldSurfaceSet mSurfaceSet;

  SurfaceMaterialResolver const* mwResolver;

  using MaterialMeshKey = std::tuple<uint64_t, bool, std::string>;

  std::map<MaterialMeshKey, uint32_t> mMaterialHashToMesh;
  std::vector<WallRenderSurface> mWallRenderSurfaces;

protected:
  mpp::mesh::MeshSpecification createMeshSpecification(
      mpp::mesh::Primitive::Type primitiveType) override;

private:
  void processMaterialDefinition(
      uint32_t index,
      bw::core::MaterialDefinition const& def,
      bool floor,
      std::optional<WallRenderVariant> const& variant,
      std::shared_ptr<mpp::ProgrammaticModelStream> modelStream,
      std::optional<uint64_t> resolvedHash = std::nullopt);

  void processSurfaceMaterial(
      bw::core::SurfaceMaterialReference const& material,
      std::string const& embossPresetId,
      bool floor,
      std::optional<WallRenderVariant> const& variant,
      std::shared_ptr<mpp::ProgrammaticModelStream> modelStream);

public:
  WorldBatch(std::string const& name, mpp::ResourcePtr textureOrMaterial, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr, bw::core::World const* world, WorldSurfaceSet surfaceSet, SurfaceMaterialResolver const* resolver, std::vector<WallRenderSurface> wallRenderSurfaces = {});

  std::shared_ptr<mpp::ModelStream> createModelStream() override;

  uint32_t getMeshIndexForMaterialHash(
      uint64_t hashValue, bool floor,
      std::optional<WallRenderVariant> const& variant = std::nullopt) const;

  bool hasMeshForMaterialHash(
      uint64_t hashValue, bool floor,
      std::optional<WallRenderVariant> const& variant = std::nullopt) const;

  size_t getMaterialMeshCount() const;

  std::string formatMeshName(
      uint64_t hashValue, bool floor,
      std::optional<WallRenderVariant> const& variant = std::nullopt) const;

  void finishUpdate(uint32_t meshIndex, uint32_t numTriangles, size_t numVertices, bool updateFixedBuffers);
};
