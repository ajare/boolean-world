#include <format>

#include <mpp/mesh/VertexTypeSpecification.h>

#include <core/Defines.h>
#include <core/LiquidType.h>

#include "WorldBatch.h"

using namespace std;

namespace {
string variantIdentity(optional<WallRenderVariant> const& variant) {
  if (variant && variant->identity.empty()) {
    throw invalid_argument("Wall render variants must have a non-empty identity.");
  }
  return variant ? variant->identity : string{};
}
}  // namespace

mpp::mesh::MeshSpecification WorldBatch::createMeshSpecification(
    mpp::mesh::Primitive::Type primitiveType) {
  auto specification = TriangleBatch::createMeshSpecification(primitiveType);
  specification.getVertexBufferAttributeLayout(0).createAttribute(
      mpp::mesh::Vertex::Component::UserDefined1, "LIQUID_SURFACE_HEIGHT",
      mpp::mesh::Vertex::DataType::Float, false);
  return specification;
}

WorldBatch::WorldBatch(string const& name, mpp::ResourcePtr textureOrMaterial, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr, bw::core::World const* world, WorldSurfaceSet surfaceSet, SubMaterialResolver const* resolver, vector<WallRenderSurface> wallRenderSurfaces)
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
      mSurfaceSet(surfaceSet),
      mwResolver(resolver),
      mWallRenderSurfaces(move(wallRenderSurfaces)) {
  for (auto const& surface : mWallRenderSurfaces) {
    if (!surface.variant) {
      throw invalid_argument("Wall render surface catalog entries must carry a variant.");
    }
    (void)variantIdentity(surface.variant);
  }
}

void WorldBatch::processMaterialDefinition(
    uint32_t index,
    bw::core::MaterialDefinition const& def,
    bool floor,
    optional<WallRenderVariant> const& variant,
    shared_ptr<mpp::ProgrammaticModelStream> modelStream) {
  if (floor && variant) {
    throw invalid_argument("Wall render variants cannot be used by horizontal surfaces.");
  }
  auto hashValue = def.data.hash(index);
  MaterialMeshKey key{hashValue, floor, variantIdentity(variant)};

  if (mMaterialHashToMesh.find(key) == mMaterialHashToMesh.end()) {
    auto const& spec = getSpecification();

    auto meshIndex = modelStream->createMesh(
        formatMeshName(hashValue, floor, variant), spec,
        getMaterial()->getName(), getIndexWidth(), getPointSize());
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

void WorldBatch::processSubMaterial(
    string const& subMaterialId,
    bool floor,
    optional<WallRenderVariant> const& variant,
    shared_ptr<mpp::ProgrammaticModelStream> modelStream) {
  auto resolved = mwResolver->resolve(subMaterialId);
  bw::core::MaterialDefinition def;
  def.data = resolved.def;

  processMaterialDefinition(resolved.materialIndex, def, floor, variant, modelStream);
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
      processSubMaterial(properties.floorMaterialId, true, nullopt, modelStream);
      processSubMaterial(properties.ceilingMaterialId, false, nullopt, modelStream);
    } else {
      processSubMaterial(properties.wallMaterialId, false, nullopt, modelStream);
    }
  }

  // Seed the extra wall buckets independently of their Sub-material. This
  // lets distinct surface variants bind distinct state without making a
  // Sub-material itself wall-specific.
  if (mSurfaceSet == WorldSurfaceSet::Walls) {
    for (auto const& surface : mWallRenderSurfaces) {
      processSubMaterial(
          surface.subMaterialId, false, surface.variant, modelStream);
    }
  }

  // A wall's back face (the side its normal points away from) always
  // renders as this reserved, plain-white material, regardless of any
  // Primitive's authored wallMaterialId - so its mesh bucket needs to
  // exist even for a World with no Primitives yet.
  if (mSurfaceSet != WorldSurfaceSet::Horizontal) {
    processMaterialDefinition(
        BW_WALL_BACK_FACE_MATERIAL_INDEX, bw::core::MaterialDefinition{},
        false, nullopt, modelStream);
  }

  // A liquid surface always renders as one of these reserved materials,
  // regardless of any Primitive's authored floorMaterialId - so every liquid
  // type's mesh bucket needs to exist even for a World with no Primitives
  // yet, since which type wets a given face isn't known until then.
  if (mSurfaceSet == WorldSurfaceSet::Horizontal) {
    for (int32_t i = 0; i < bw::core::LiquidTypeCount; ++i) {
      processMaterialDefinition(
          bw::core::LiquidMaterialIndex(static_cast<bw::core::LiquidType>(i)),
          bw::core::MaterialDefinition{}, true, nullopt, modelStream);
    }
  }

  return modelStream;
}

uint32_t WorldBatch::getMeshIndexForMaterialHash(
    uint64_t hashValue, bool floor,
    optional<WallRenderVariant> const& variant) const {
  auto it = mMaterialHashToMesh.find(
      {hashValue, floor, variantIdentity(variant)});

  return it == mMaterialHashToMesh.end() ? 0u : it->second;
}

bool WorldBatch::hasMeshForMaterialHash(
    uint64_t hashValue, bool floor,
    optional<WallRenderVariant> const& variant) const {
  return mMaterialHashToMesh.contains(
      {hashValue, floor, variantIdentity(variant)});
}

size_t WorldBatch::getMaterialMeshCount() const {
  return mMaterialHashToMesh.size();
}

string WorldBatch::formatMeshName(
    uint64_t hashValue, bool floor,
    optional<WallRenderVariant> const& variant) const {
  return format(
      "WorldMaterial-{}-{}{}{}_Batch_Mesh", hashValue,
      floor ? "Floor" : "NonFloor",
      variant ? "-Variant-" : "", variant ? variant->identity : "");
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