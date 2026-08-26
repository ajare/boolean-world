#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#pragma warning(pop)

namespace editor {

// One closed border to draw, in its own colour: a flat list of endpoint pairs
// in the renderer's 3D space (height in y).
struct PreviewOutline {
  std::vector<glm::vec3> segments;
  glm::vec4 colour{1.0f};
};

// Draws world-space line segments straight over a finished render-pipeline
// image, with depth testing off, so what it draws is always on top of the
// shaded world rather than being hidden by it. The 3D preview uses it to
// outline the selected surface and the one under the pointer.
//
// It owns a framebuffer of its own and attaches the pipeline's output texture
// to it per draw, because mpp keeps its render targets' framebuffers private -
// there is no supported way to make the pipeline's own target current again
// after renderScene has finished with it. Nothing else here touches mpp: it is
// a small raw-GL pass over an image mpp happens to have produced.
//
// Requires a current GL context in both the constructor and the destructor,
// like every other GPU resource the preview owns.
class PreviewOutlineRenderer {
public:
  // Throws std::runtime_error if the program will not compile or link.
  PreviewOutlineRenderer();
  ~PreviewOutlineRenderer();

  PreviewOutlineRenderer(PreviewOutlineRenderer const&) = delete;
  PreviewOutlineRenderer& operator=(PreviewOutlineRenderer const&) = delete;

  // Draws each outline in the order given, so a later one wins where two
  // overlap. Does nothing when there is nothing to draw. Every piece of GL
  // state it changes is restored before it returns, so it can run between
  // mpp's own passes and ImGui's without disturbing either.
  void render(
      std::uint32_t targetTextureId,
      std::size_t width,
      std::size_t height,
      glm::mat4 const& viewProjection,
      std::vector<PreviewOutline> const& outlines);

private:
  std::uint32_t mProgram{};
  std::uint32_t mVertexArray{};
  std::uint32_t mVertexBuffer{};
  std::uint32_t mFrameBuffer{};
  // Grown, never shrunk: an outline is a few hundred vertices at most, and
  // reallocating the store every frame the pointer moves is pure churn.
  std::size_t mVertexCapacity{};
  int mViewProjectionUniform{-1};
  int mColourUniform{-1};
};

}  // namespace editor
