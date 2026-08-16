#include "willpower/viz/Renderer.h"

namespace WP_NAMESPACE {
namespace viz {

using namespace std;
using namespace wp;

Renderer::Renderer(string const& name, string const& type, mpp::ResourceManager* renderResourceMgr)
    : ResourceWrangler(name), mName(name), mType(type), mRenderResourceMgr(renderResourceMgr), mInScene(false) {
}

string const& Renderer::getName() const {
  return mName;
}

string const& Renderer::getType() const {
  return mType;
}

shared_ptr<RenderParams> Renderer::getParams() {
  return mParams;
}

shared_ptr<mpp::ModelRenderParams> Renderer::getModelRenderParams() {
  return mSceneModel->getParams();
}

void Renderer::setRenderParams(shared_ptr<mpp::ModelRenderParams> params) {
  if (!mParams) {
    auto p = createRenderParams(params);
    mParams = shared_ptr<RenderParams>(p);
  }
}

void Renderer::updateRenderParams() {
}

void Renderer::onAddedToScene() {
}

void Renderer::addToScene(mpp::ScenePtr scene, int renderOrder) {
  if (mInScene) {
    return;
  }

  mSceneModel = createSceneModelInScene(scene, renderOrder);
  setRenderParams(mSceneModel->getParams());

  onAddedToScene();
  updateRenderParams();
  mInScene = true;
}

void Renderer::removeFromScene(mpp::ScenePtr scene) {
  removeSceneModelFromScene(scene, mSceneModel);
  mInScene = false;
}

void Renderer::update(BoundingBox const& viewBounds, float frameTime) {
  WP_UNUSED(viewBounds);
  WP_UNUSED(frameTime);
}

void Renderer::setVisible(bool visible) {
  mSceneModel->setVisible(visible);
}

bool Renderer::isVisible() const {
  return mSceneModel->isVisible();
}

void Renderer::setOrigin(wp::Vector2 const& origin) {
  mSceneModel->setOrigin(glm::vec2(origin.x, origin.y));
}

wp::Vector2 Renderer::getOrigin() const {
  auto const& origin = mSceneModel->getOrigin();
  return wp::Vector2(origin.x, origin.y);
}

void Renderer::setOffset(wp::Vector2 const& offset) {
  mSceneModel->setOffset(glm::vec2(offset.x, offset.y));
}

wp::Vector2 Renderer::getOffset() const {
  auto const& offset = mSceneModel->getOffset();
  return wp::Vector2(offset.x, offset.y);
}

void Renderer::setAngle(float angle) {
  mSceneModel->setAngle(angle);
}

float Renderer::getAngle() const {
  return mSceneModel->getAngle();
}

void Renderer::setOrbitAngle(float angle) {
  mSceneModel->setOrbitAngle(angle);
}

float Renderer::getOrbitAngle() const {
  return mSceneModel->getOrbitAngle();
}

void Renderer::setScale(Vector2 const& scale) {
  mSceneModel->setScale(glm::vec2(scale.x, scale.y));
}

wp::Vector2 Renderer::getScale() const {
  auto const& scale = mSceneModel->getScale();
  return wp::Vector2(scale.x, scale.y);
}

void Renderer::setScreenSpace(bool screenSpace) {
  mSceneModel->setScreenSpace(screenSpace);
}

bool Renderer::inScreenSpace() const {
  return mSceneModel->inScreenSpace();
}

}  // namespace viz
}  // namespace WP_NAMESPACE
