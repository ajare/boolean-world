#pragma once

#include <functional>
#include <map>
#include <utility>
#include <vector>

#include <mpp/TriangleBatch.h>
#include <mpp/ProgrammaticModelStream.h>

#include <core/World.h>
#include <core/MaterialDefinition.h>

#include "SubMaterialResolver.h"

enum class WorldSurfaceSet {
  Horizontal,
  Walls,
};

class WorldBatch : public mpp::TriangleBatch {
  bw::core::World const* mWorld;
  WorldSurfaceSet mSurfaceSet;

  SubMaterialResolver const* mwResolver;

  using MaterialMeshKey = std::pair<uint64_t, bool>;

  std::map<MaterialMeshKey, uint32_t> mMaterialHashToMesh;

private:
  void processMaterialDefinition(
      uint32_t index,
      bw::core::MaterialDefinition const& def,
      bool floor,
      std::shared_ptr<mpp::ProgrammaticModelStream> modelStream);

  void processSubMaterial(
      std::string const& subMaterialId,
      bool floor,
      std::shared_ptr<mpp::ProgrammaticModelStream> modelStream);

public:
  WorldBatch(std::string const& name, mpp::ResourcePtr textureOrMaterial, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr, bw::core::World const* world, WorldSurfaceSet surfaceSet, SubMaterialResolver const* resolver);

  std::shared_ptr<mpp::ModelStream> createModelStream() override;

  uint32_t getMeshIndexForMaterialHash(uint64_t hashValue, bool floor) const;

  size_t getMaterialMeshCount() const;

  std::string formatMeshName(uint64_t hashValue, bool floor) const;

  void finishUpdate(uint32_t meshIndex, uint32_t numTriangles, size_t numVertices, bool updateFixedBuffers);
};
