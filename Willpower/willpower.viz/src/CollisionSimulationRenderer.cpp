#include <mpp/ProgrammaticMaterialStream.h>

#include "willpower/common/Exceptions.h"

#include "willpower/viz/CollisionSimulationRenderer.h"

namespace WP_NAMESPACE {
namespace viz {

using namespace std;
using namespace wp;

CollisionSimulationRenderer::CollisionSimulationRenderer(string const& name, collide::Simulation* simulation, size_t indexWidth, mpp::ResourceManager* renderResourceMgr)
    : StaticRenderer(name, "CollisionSimulationRenderer", StaticRenderer::GridOptions(), indexWidth, renderResourceMgr), mwSimulation(simulation) {
}

void CollisionSimulationRenderer::getExtents(Vector2& minExtent, Vector2& maxExtent) {
  mwSimulation->getExtents(minExtent, maxExtent);
}

void CollisionSimulationRenderer::createMeshSpecifications() {
  mpp::mesh::MeshSpecification lineMeshSpec(mpp::mesh::Primitive::Type::Lines);

  auto attribLayout = lineMeshSpec.createVertexBufferAttributeLayout(false);
  attribLayout->createAttribute(mpp::mesh::Vertex::Component::Position2, mpp::mesh::Vertex::DataType::Float, false);
  attribLayout->createAttribute(mpp::mesh::Vertex::Component::Colour4, mpp::mesh::Vertex::DataType::UnsignedByte, true);
  lineMeshSpec.setStorageType(mpp::mesh::VertexBufferStorageType::Static);
  lineMeshSpec.setIndexedVertices(false);

  mMeshSpecifications["Lines"] = lineMeshSpec;
}

void CollisionSimulationRenderer::createMaterials(mpp::ResourceManager* resourceMgr) {
  // Lines
  auto programResource = resourceMgr->getDefault2dProgram(
      mMeshSpecifications["Lines"],
      MPP_PROGRAM_TAGS_PRIM_LINES | MPP_PROGRAM_TAGS_DIFFUSE,
      false,
      getType());

  auto matStream = make_shared<mpp::ProgrammaticMaterialStream>(resourceMgr);

  matStream->setProgram(programResource->getName());

  addMaterialResource("Lines", resourceMgr->declareResource(getName() + "_Lines", matStream).first);
}

void CollisionSimulationRenderer::createMeshes(mpp::ProgrammaticModelStream* stream, mpp::ResourceManager* resourceMgr) {
  WP_UNUSED(resourceMgr);

  auto const& linesMeshSpec = mMeshSpecifications["Lines"];

  auto linesMeshId = stream->createMesh("Lines", linesMeshSpec, mMaterialNames["Lines"], (int)mIndexWidth);

  auto numStaticLines = mwSimulation->getNumStaticLines();
  mpp::mesh::VertexData lineVertexData(linesMeshSpec, numStaticLines * 2);

  Vector2 t0{0.0f, 0.0f}, t1{1.0f, 0.0f};
  float colour[4] = {1.0f, 1.0f, 1.0f, 1.0f};

  for (uint32_t i = 0; i < numStaticLines; ++i) {
    auto const& line = mwSimulation->getStaticLine(i);

    Vector2 v0, v1;
    line.getVertices(v0, v1);

    addVertexData(&lineVertexData, v0, t0, colour);
    addVertexData(&lineVertexData, v1, t1, colour);
  }

  stream->addVertexData(linesMeshId, lineVertexData);
}

RenderParams* CollisionSimulationRenderer::createRenderParams(shared_ptr<mpp::ModelRenderParams> params) {
  return new CollisionSimulationRenderParams(params);
}

void CollisionSimulationRenderer::updateRenderParams() {
  auto renderParams = static_cast<wp::viz::CollisionSimulationRenderParams*>(getParams().get());
  auto modelRenderParams = getModelRenderParams();

  modelRenderParams->setMeshFlags("Lines", renderParams->getRender() ? mpp::ModelRenderParams::Flag_Visible : 0);
}

}  // namespace viz
}  // namespace WP_NAMESPACE
