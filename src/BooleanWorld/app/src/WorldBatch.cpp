#include <format>

#include <mpp/mesh/VertexTypeSpecification.h>

#include "WorldBatch.h"

using namespace std;

WorldBatch::WorldBatch(string const& name, mpp::ResourcePtr textureOrMaterial, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr, bw::core::World const* world, WorldSurfaceSet surfaceSet)
    : TriangleBatch(name,
                    {mpp::TriangleBatchOptions::Dimension::P3D,
                     true,
                     mpp::mesh::DataTypeFloat::vertexDataType(),
                     {mpp::mesh::DataTypeFloat::vertexDataType(), false},
                     {mpp::mesh::DataTypeUnsignedByte::vertexDataType(), false},
                     false,
                     true},
                    16,
                    textureOrMaterial,
                    0,
                    renderSystem,
                    resourceMgr),
      mWorld(world),
      mSurfaceSet(surfaceSet) {
}

void WorldBatch::processMaterialDefinition(
    uint32_t index,
    bw::core::MaterialDefinition const& def,
    bool floor,
    shared_ptr<mpp::ProgrammaticModelStream> modelStream) {
  auto hashValue = def.data.hash(index);
  MaterialMeshKey key{hashValue, floor};

  if (mMaterialHashToMesh.find(key) == mMaterialHashToMesh.end()) {
    auto const& spec = getSpecification();

    auto meshIndex = modelStream->createMesh(formatMeshName(hashValue, floor), spec, getMaterial()->getName(), getIndexWidth(), getPointSize());
    auto numVertices = getVertexCount(mInitialCapacity);

    if (numVertices > 0) {
      modelStream->addVertexData(meshIndex, mpp::mesh::VertexData(spec, numVertices));
    }

    if (spec.verticesIndexed()) {
      addIndexedPrimitives(modelStream, (int)meshIndex);
    }

    mMaterialHashToMesh[key] = (uint32_t)meshIndex;
  }
}

shared_ptr<mpp::ModelStream> WorldBatch::createModelStream() {
  auto modelStream = make_shared<mpp::ProgrammaticModelStream>(mResourceMgr);
  modelStream->setCalculateBounds(false);

  // Create meshes for each material
  auto numPrimitives = mWorld->getNumPrimitives();

  for (uint32_t i = 0; i < numPrimitives; ++i) {
    auto primitive = mWorld->getPrimitive(i);
    auto const& properties = primitive->getProperties();

    if (mSurfaceSet == WorldSurfaceSet::Horizontal) {
      processMaterialDefinition(
          properties.floorMaterialIndex, properties.floorMaterialDef, true,
          modelStream);
      processMaterialDefinition(
          properties.ceilingMaterialIndex, properties.ceilingMaterialDef, false,
          modelStream);
    } else {
      processMaterialDefinition(
          properties.wallMaterialIndex, properties.wallMaterialDef, false,
          modelStream);
    }
  }

  return modelStream;
}

uint32_t WorldBatch::getMeshIndexForMaterialHash(
    uint64_t hashValue, bool floor) const {
  auto it = mMaterialHashToMesh.find({hashValue, floor});

  return it == mMaterialHashToMesh.end() ? 0u : it->second;
}

size_t WorldBatch::getMaterialMeshCount() const {
  return mMaterialHashToMesh.size();
}

string WorldBatch::formatMeshName(uint64_t hashValue, bool floor) const {
  return format(
      "WorldMaterial-{}-{}_Batch_Mesh", hashValue,
      floor ? "Floor" : "NonFloor");
}

void WorldBatch::finishUpdate(uint32_t meshIndex, uint32_t numTriangles, size_t numVertices, bool updateFixedBuffers) {
  mMeshes[meshIndex].curCount = numTriangles;

  auto mesh = static_pointer_cast<mpp::Model>(getModel())->getMesh(meshIndex);

  if (numTriangles > 0) {
    if (mesh->isIndexed()) {
      mesh->mapIndexData(numTriangles);
    }

    for (size_t i = 0; i < mesh->getNumVertexBuffers(); ++i) {
      auto vertexBuffer = mesh->getVertexBuffer((int)i);

      if (updateFixedBuffers || !vertexBuffer->isStatic()) {
        vertexBuffer->mapBufferData(numVertices);
      }
    }
  }

  mesh->setNumPrimitives(numTriangles);
}