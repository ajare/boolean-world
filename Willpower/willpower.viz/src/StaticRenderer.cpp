#include <mpp/ProgrammaticMaterialStream.h>

#include "willpower/common/Exceptions.h"

#include "willpower/viz/StaticRenderer.h"

namespace WP_NAMESPACE {
namespace viz {

using namespace std;
using namespace wp;

StaticRenderer::StaticRenderer(string const& name, string const& type, GridOptions const& gridOptions, size_t indexWidth, mpp::ResourceManager* renderResourceMgr)
    : Renderer(name, type, renderResourceMgr), mGridOptions(gridOptions), mIndexWidth(indexWidth), mUseIncludeMasking(false), mUseExcludeMasking(false) {
}

StaticRenderer::~StaticRenderer() {
  // Need to do this to release the model.
  mSceneModel.reset();

  // Now release again.
  mModel->release(this);

  if (mModel && !mModel->getRefCount()) {
    mModel->destroy();
    mRenderResourceMgr->deleteResource(mModel->getName());
  }

  for (auto material : mMaterials) {
    material->release(this);

    if (material && !material->getRefCount()) {
      material->destroy();
      mRenderResourceMgr->deleteResource(material->getName());
    }
  }
}

bool StaticRenderer::usingLookupGrid() const {
  return mGridOptions.useGrid;
}

void StaticRenderer::addVertexData(mpp::mesh::VertexData* vertexData, Vector2 const& pos, Vector2 const& tex, float colour[4]) {
  auto const& meshSpec = vertexData->getMeshSpecification();

  // Pack the VertexData based on the MeshSpecification
  for (size_t i = 0; i < meshSpec.getNumVertexBufferAttributeLayouts(); ++i) {
    auto const& layout = meshSpec.getVertexBufferAttributeLayout((uint32_t)i);
    for (size_t j = 0; j < layout.getNumAttributes(); ++j) {
      auto const& attrib = layout.getAttribute(j);

      switch (attrib.component) {
        case mpp::mesh::Vertex::Component::Position2:
          switch (attrib.dataType) {
            case mpp::mesh::Vertex::DataType::HalfFloat:
              vertexData->f16(pos.x, pos.y);
              break;
            case mpp::mesh::Vertex::DataType::Float:
              vertexData->f32(pos.x, pos.y);
              break;
            default:
              break;  // Should never get here
          }
          break;
        case mpp::mesh::Vertex::Component::TexCoord2:
          switch (attrib.dataType) {
            case mpp::mesh::Vertex::DataType::HalfFloat:
              vertexData->f16(tex.x, tex.y);
              break;
            case mpp::mesh::Vertex::DataType::Float:
              vertexData->f32(tex.x, tex.y);
              break;
            default:
              break;  // Should never get here
          }
          break;
        case mpp::mesh::Vertex::Component::Colour1:
          switch (attrib.dataType) {
            case mpp::mesh::Vertex::DataType::UnsignedByte:
              vertexData->u8((uint8_t)(colour[0] * 255.0f));
              break;
            case mpp::mesh::Vertex::DataType::HalfFloat:
              vertexData->f16(colour[0]);
              break;
            case mpp::mesh::Vertex::DataType::Float:
              vertexData->f32(colour[0]);
              break;
            default:
              break;  // Should never get here
          }
          break;
        case mpp::mesh::Vertex::Component::Colour3:
          switch (attrib.dataType) {
            case mpp::mesh::Vertex::DataType::UnsignedByte:
              vertexData->u8((uint8_t)(colour[0] * 255.0f), (uint8_t)(colour[1] * 255.0f), (uint8_t)(colour[2] * 255.0f));
              break;
            case mpp::mesh::Vertex::DataType::HalfFloat:
              vertexData->f16(colour[0], colour[1], colour[2]);
              break;
            case mpp::mesh::Vertex::DataType::Float:
              vertexData->f32(colour[0], colour[1], colour[2]);
              break;
            default:
              break;  // Should never get here
          }
          break;
        case mpp::mesh::Vertex::Component::Colour4:
          switch (attrib.dataType) {
            case mpp::mesh::Vertex::DataType::UnsignedByte:
              vertexData->u8((uint8_t)(colour[0] * 255.0f), (uint8_t)(colour[1] * 255.0f), (uint8_t)(colour[2] * 255.0f), (uint8_t)(colour[3] * 255.0f));
              break;
            case mpp::mesh::Vertex::DataType::HalfFloat:
              vertexData->f16(colour[0], colour[1], colour[2], colour[3]);
              break;
            case mpp::mesh::Vertex::DataType::Float:
              vertexData->f32(colour[0], colour[1], colour[2], colour[3]);
              break;
            default:
              break;  // Should never get here
          }
          break;
      }
    }
  }
}

void StaticRenderer::addVertexData(mpp::mesh::VertexData* vertexData, Vector2 const& pos0, Vector2 const& pos1, Vector2 const& tex0, Vector2 const& tex1, float colour[4]) {
  auto const& meshSpec = vertexData->getMeshSpecification();

  // Pack the VertexData based on the MeshSpecification
  for (size_t i = 0; i < meshSpec.getNumVertexBufferAttributeLayouts(); ++i) {
    auto const& layout = meshSpec.getVertexBufferAttributeLayout((uint32_t)i);
    for (size_t j = 0; j < layout.getNumAttributes(); ++j) {
      auto const& attrib = layout.getAttribute(j);

      switch (attrib.component) {
        case mpp::mesh::Vertex::Component::Position2:
          switch (attrib.dataType) {
            case mpp::mesh::Vertex::DataType::HalfFloat:
              vertexData->f16(pos0.x, pos0.y);
              break;
            case mpp::mesh::Vertex::DataType::Float:
              vertexData->f32(pos0.x, pos0.y);
              break;
            default:
              break;  // Should never get here
          }
          break;
        case mpp::mesh::Vertex::Component::Position4:
          switch (attrib.dataType) {
            case mpp::mesh::Vertex::DataType::HalfFloat:
              vertexData->f16(pos0.x, pos0.y, pos1.x, pos1.y);
              break;
            case mpp::mesh::Vertex::DataType::Float:
              vertexData->f32(pos0.x, pos0.y, pos1.x, pos1.y);
              break;
            default:
              break;  // Should never get here
          }
          break;
        case mpp::mesh::Vertex::Component::TexCoord2:
          switch (attrib.dataType) {
            case mpp::mesh::Vertex::DataType::HalfFloat:
              vertexData->f16(tex0.x, tex0.y);
              break;
            case mpp::mesh::Vertex::DataType::Float:
              vertexData->f32(tex0.x, tex0.y);
              break;
            default:
              break;  // Should never get here
          }
          break;
        case mpp::mesh::Vertex::Component::TexCoord4:
          switch (attrib.dataType) {
            case mpp::mesh::Vertex::DataType::HalfFloat:
              vertexData->f16(tex0.x, tex0.y, tex1.x, tex1.y);
              break;
            case mpp::mesh::Vertex::DataType::Float:
              vertexData->f32(tex0.x, tex0.y, tex1.x, tex1.y);
              break;
            default:
              break;  // Should never get here
          }
          break;
        case mpp::mesh::Vertex::Component::Colour1:
          switch (attrib.dataType) {
            case mpp::mesh::Vertex::DataType::UnsignedByte:
              vertexData->u8((uint8_t)(colour[0] * 255.0f));
              break;
            case mpp::mesh::Vertex::DataType::HalfFloat:
              vertexData->f16(colour[0]);
              break;
            case mpp::mesh::Vertex::DataType::Float:
              vertexData->f32(colour[0]);
              break;
            default:
              break;  // Should never get here
          }
          break;
        case mpp::mesh::Vertex::Component::Colour3:
          switch (attrib.dataType) {
            case mpp::mesh::Vertex::DataType::UnsignedByte:
              vertexData->u8((uint8_t)(colour[0] * 255.0f), (uint8_t)(colour[1] * 255.0f), (uint8_t)(colour[2] * 255.0f));
              break;
            case mpp::mesh::Vertex::DataType::HalfFloat:
              vertexData->f16(colour[0], colour[1], colour[2]);
              break;
            case mpp::mesh::Vertex::DataType::Float:
              vertexData->f32(colour[0], colour[1], colour[2]);
              break;
            default:
              break;  // Should never get here
          }
          break;
        case mpp::mesh::Vertex::Component::Colour4:
          switch (attrib.dataType) {
            case mpp::mesh::Vertex::DataType::UnsignedByte:
              vertexData->u8((uint8_t)(colour[0] * 255.0f), (uint8_t)(colour[1] * 255.0f), (uint8_t)(colour[2] * 255.0f), (uint8_t)(colour[3] * 255.0f));
              break;
            case mpp::mesh::Vertex::DataType::HalfFloat:
              vertexData->f16(colour[0], colour[1], colour[2], colour[3]);
              break;
            case mpp::mesh::Vertex::DataType::Float:
              vertexData->f32(colour[0], colour[1], colour[2], colour[3]);
              break;
            default:
              break;  // Should never get here
          }
          break;
      }
    }
  }
}

StaticRenderer::GridOptions::CellStrategy StaticRenderer::getGridCellStrategy() const {
  return mGridOptions.strategy;
}

void StaticRenderer::addMaterialResource(string const& lookupName, mpp::ResourcePtr resource) {
  auto resName = resource->getName();

  if (mMaterialNames.find(resName) == mMaterialNames.end()) {
    mMaterials.push_back(resource);
    resource->acquire(this);
  }

  mMaterialNames[lookupName] = resName;
}

shared_ptr<AccelerationGrid> StaticRenderer::getLookupGrid() {
  return mLookupGrid;
}

void StaticRenderer::createLookupGrid() {
  Vector2 gridMin, gridMax;
  getExtents(gridMin, gridMax);

  mLookupGrid.reset();

  // Pad grid out to mLookupGridCellSize
  auto cellSize = Vector2(mGridOptions.cellSize, mGridOptions.cellSize);
  auto gridSize = gridMax - gridMin;

  auto padding = (cellSize - gridSize.f_mod(cellSize)) * mGridOptions.paddingPct;
  auto paddedSize = (gridSize + padding);
  auto paddedOffset = gridMin - padding / 2;

  auto numCells = paddedSize / cellSize;
  mLookupGrid = make_shared<AccelerationGrid>(paddedOffset, paddedSize, (int)numCells.x, (int)numCells.y);
}

mpp::SceneModel2dPtr StaticRenderer::createSceneModelInScene(mpp::ScenePtr scene, int renderOrder) {
  return scene->add2dModel(mModel, renderOrder);
}

void StaticRenderer::removeSceneModelFromScene(mpp::ScenePtr scene, mpp::SceneModel2dPtr sceneModel) {
  scene->remove2dModel(sceneModel);
}

void StaticRenderer::build(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) {
  WP_UNUSED(renderSystem);

  // MeshSpecifications
  createMeshSpecifications();

  // Materials
  mMaterials.clear();
  createMaterials(resourceMgr);

  // Model streams
  if (usingLookupGrid()) {
    createLookupGrid();
  }

  auto ms = make_shared<mpp::ProgrammaticModelStream>(resourceMgr);
  createMeshes(ms.get(), resourceMgr);

  // Create resource
  mModel = resourceMgr->declareResource(getName() + "_Model", ms).first;
  mModel->acquire(this);
}

void StaticRenderer::useGridIncludeMasking(bool use) {
  mUseIncludeMasking = use;
}

bool StaticRenderer::usingGridIncludeMasking() const {
  return mUseIncludeMasking;
}

void StaticRenderer::useGridExcludeMasking(bool use) {
  mUseExcludeMasking = use;
}

bool StaticRenderer::usingGridExcludeMasking() const {
  return mUseExcludeMasking;
}

void StaticRenderer::clearGridIncludeMask() {
  mGridIncludeMask.clear();
}

void StaticRenderer::clearGridExcludeMask() {
  mGridExcludeMask.clear();
}

void StaticRenderer::addToGridIncludeMask(uint32_t id) {
  mGridIncludeMask.insert(id);
}

void StaticRenderer::addToGridExcludeMask(uint32_t id) {
  mGridExcludeMask.insert(id);
}

bool StaticRenderer::gridIdInIncludeMask(uint32_t id) const {
  return mGridIncludeMask.find(id) != mGridIncludeMask.end();
}

bool StaticRenderer::gridIdInExcludeMask(uint32_t id) const {
  return mGridExcludeMask.find(id) != mGridExcludeMask.end();
}

}  // namespace viz
}  // namespace WP_NAMESPACE
