#include <mpp/ProgrammaticBasicMaterialStream.h>

#include "willpower/common/Exceptions.h"

#include "willpower/viz/FirepowerMeshRenderer.h"

namespace WP_NAMESPACE {
namespace viz {

using namespace std;
using namespace wp;

FirepowerMeshRenderer::FirepowerMeshRenderer(string const& name, firepower::MeshCollisionManager* meshCollisionMgr, size_t indexWidth, mpp::ResourceManager* renderResourceMgr)
    : StaticRenderer(name, "FirepowerMeshRenderer", StaticRenderer::GridOptions(), indexWidth, renderResourceMgr), mwMeshCollisionMgr(meshCollisionMgr) {
}

void FirepowerMeshRenderer::getExtents(Vector2& minExtent, Vector2& maxExtent) {
  mwMeshCollisionMgr->getExtents(minExtent, maxExtent);
}

void FirepowerMeshRenderer::createMeshSpecifications() {
  mpp::mesh::MeshSpecification lineMeshSpec(mpp::mesh::Primitive::Type::Lines);

  auto attribLayout = lineMeshSpec.createVertexBufferAttributeLayout(false);
  attribLayout->createAttribute(mpp::mesh::Vertex::Component::Position2, mpp::mesh::Vertex::DataType::Float, false);
  attribLayout->createAttribute(mpp::mesh::Vertex::Component::Colour4, mpp::mesh::Vertex::DataType::UnsignedByte, true);
  lineMeshSpec.setStorageType(mpp::mesh::VertexBufferStorageType::Static);
  lineMeshSpec.setIndexedVertices(false);

  mMeshSpecifications["Lines"] = lineMeshSpec;
}

void FirepowerMeshRenderer::createMaterials(mpp::ResourceManager* resourceMgr) {
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

void FirepowerMeshRenderer::createMeshes(mpp::ProgrammaticModelStream* stream, mpp::ResourceManager* resourceMgr) {
  WP_UNUSED(resourceMgr);

  auto const& linesMeshSpec = mMeshSpecifications["Lines"];

  auto linesMeshId = stream->createMesh("Lines", linesMeshSpec, mMaterialNames["Lines"], (int)mIndexWidth);

  auto numEdges = (uint32_t)mwMeshCollisionMgr->getNumCollisionEdges();
  mpp::mesh::VertexData lineVertexData(linesMeshSpec, numEdges * 2);

  Vector2 t0{0.0f, 0.0f}, t1{1.0f, 0.0f};
  float colour[4] = {1.0f, 1.0f, 1.0f, 1.0f};

  for (uint32_t i = 0; i < numEdges; ++i) {
    auto const& edge = mwMeshCollisionMgr->getCollisionEdge(i);

    addVertexData(&lineVertexData, edge.v[0], t0, colour);
    addVertexData(&lineVertexData, edge.v[1], t1, colour);
  }

  stream->addVertexData(linesMeshId, lineVertexData);
}

RenderParams* FirepowerMeshRenderer::createRenderParams(shared_ptr<mpp::ModelRenderParams> params) {
  return new FirepowerMeshRenderParams(params);
}

void FirepowerMeshRenderer::updateRenderParams() {
  auto renderParams = static_cast<wp::viz::FirepowerMeshRenderParams*>(getParams().get());
  auto modelRenderParams = getModelRenderParams();

  modelRenderParams->setMeshFlags("Lines", renderParams->getRender() ? mpp::ModelRenderParams::Flag_Visible : 0);
}

}  // namespace viz
}  // namespace WP_NAMESPACE
