#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/gtx/rotate_vector.hpp>
#pragma warning(pop)

#include "ReactiveCamera.h"

ReactiveCamera::ReactiveCamera(glm::vec3 const& position, float yaw, float pitch, float fov, float aspectRatio)
    : mpp::helper::FpsCamera(position, yaw, pitch, fov, aspectRatio) {
}

void ReactiveCamera::setPosition(glm::vec3 const& position) {
  mPosition = position;
  mDirty = true;
}

void ReactiveCamera::setYaw(float yaw) {
  mYaw = yaw;
  mDirty = true;
}

void ReactiveCamera::setMirrored(bool mirrored) {
  if (mMirrored == mirrored) return;
  mMirrored = mirrored;
  markCut();
}

glm::mat4 ReactiveCamera::getViewTransform() {
  auto view = mpp::helper::FpsCamera::getViewTransform();
  // Reflect camera-right, not world-up or the forward direction. Yaw alone
  // cannot represent the negative determinant of a mirror mapping.
  if (mMirrored) {
    for (int column = 0; column < 4; ++column) view[column][0] *= -1.0f;
  }
  return view;
}

void ReactiveCamera::setPitch(float pitch) {
  mPitch = pitch;
  mDirty = true;
}