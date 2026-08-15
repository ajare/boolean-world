#include <mpp/ProgrammaticBasicMaterialStream.h>

#include "willpower/common/Exceptions.h"

#include "willpower/viz/AccelerationGridRenderer.h"

namespace WP_NAMESPACE {
namespace viz {

using namespace std;
using namespace wp;

AccelerationGridRenderer::AccelerationGridRenderer(string const& name, shared_ptr<AccelerationGrid> grid, size_t indexWidth, mpp::ResourceManager* renderResourceMgr)
    : StaticRenderer(name, "AccelerationGridRenderer", StaticRenderer::GridOptions(), indexWidth, renderResourceMgr), mGrid(grid) {
}

void AccelerationGridRenderer::getExtents(Vector2& minExtent, Vector2& maxExtent) {
  minExtent = mGrid->getOffset();
  maxExtent = minExtent + mGrid->getSize();
}

void AccelerationGridRenderer::createMeshSpecifications() {
  mpp::mesh::MeshSpecification lineMeshSpec(mpp::mesh::Primitive::Type::Lines);

  auto attribLayout = lineMeshSpec.createVertexBufferAttributeLayout(false);
  attribLayout->createAttribute(mpp::mesh::Vertex::Component::Position2, mpp::mesh::Vertex::DataType::Float, false);
  attribLayout->createAttribute(mpp::mesh::Vertex::Component::Colour4, mpp::mesh::Vertex::DataType::UnsignedByte, true);
  lineMeshSpec.setStorageType(mpp::mesh::VertexBufferStorageType::Static);
  lineMeshSpec.setIndexedVertices(false);

  mMeshSpecifications["Lines"] = lineMeshSpec;
}

void AccelerationGridRenderer::createMaterials(mpp::ResourceManager* resourceMgr) {
  // Lines
  auto programResource = resourceMgr->getDefault2dProgram(
      mMeshSpecifications["Lines"],
      MPP_PROGRAM_TAGS_PRIM_LINES | MPP_PROGRAM_TAGS_DIFFUSE,
      false,
      getType());

  auto matStream = make_shared<mpp::ProgrammaticBasicMaterialStream>(resourceMgr);

  matStream->setProgram(programResource->getName());

  addMaterialResource("Lines", resourceMgr->declareResource(getName() + "_Lines", matStream).first);
}

void AccelerationGridRenderer::createMeshes(mpp::ProgrammaticModelStream* stream, mpp::ResourceManager* resourceMgr) {
  WP_UNUSED(resourceMgr);

  auto const& linesMeshSpec = mMeshSpecifications["Lines"];

  auto linesMeshId = stream->createMesh("Lines", linesMeshSpec, mMaterialNames["Lines"], (int)mIndexWidth);
  size_t numLineVertices = ((mGrid->getCellDimensionX() + 1) + (mGrid->getCellDimensionY() + 1)) * 2;

  mpp::mesh::VertexData lineVertexData(linesMeshSpec, numLineVertices);

  auto const& offset = mGrid->getOffset();
  auto const& size = mGrid->getSize();
  auto const& cellSize = mGrid->getCellSize();

  Vector2 t0{0.0f, 0.0f}, t1{1.0f, 0.0f};
  float colour[4] = {1.0f, 1.0f, 1.0f, 1.0f};

  for (int i = 0; i <= mGrid->getCellDimensionX(); ++i) {
    Vector2 v0 = offset + cellSize * Vector2::UNIT_X * (float)i;
    Vector2 v1 = v0 + size * Vector2::UNIT_Y;

    addVertexData(&lineVertexData, v0, t0, colour);
    addVertexData(&lineVertexData, v1, t1, colour);
  }

  for (int i = 0; i <= mGrid->getCellDimensionY(); ++i) {
    Vector2 v0 = offset + cellSize * Vector2::UNIT_Y * (float)i;
    Vector2 v1 = v0 + size * Vector2::UNIT_X;

    addVertexData(&lineVertexData, v0, t0, colour);
    addVertexData(&lineVertexData, v1, t1, colour);
  }

  stream->addVertexData(linesMeshId, lineVertexData);
}

RenderParams* AccelerationGridRenderer::createRenderParams(shared_ptr<mpp::ModelRenderParams> params) {
  return new AccelerationGridRenderParams(params);
}

void AccelerationGridRenderer::updateRenderParams() {
  auto renderParams = static_cast<wp::viz::AccelerationGridRenderParams*>(getParams().get());
  auto modelRenderParams = getModelRenderParams();

  modelRenderParams->setMeshFlags("Lines", renderParams->getRender() ? mpp::ModelRenderParams::Flag_Visible : 0);
}

}  // namespace viz
}  // namespace WP_NAMESPACE
