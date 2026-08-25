#include <GL/glew.h>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/gtc/type_ptr.hpp>
#pragma warning(pop)

#include "PreviewHighlight.h"

namespace editor {
namespace {

constexpr float highlightLineWidth = 4.0f;

void submitVertex(PreviewVertex3 const& vertex) {
  // The game's 3D coordinates map its 2D world (X, Y) onto (X, Z).
  glVertex3f(vertex.x, vertex.z, vertex.y);
}

}  // namespace

void drawPreviewHighlight(
    std::vector<PreviewTriangle> const& triangles,
    glm::mat4 const& viewTransform,
    glm::mat4 const& projectionTransform) {
  if (triangles.empty()) {
    return;
  }

  glUseProgram(0);
  glBindTexture(GL_TEXTURE_2D, 0);

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadMatrixf(glm::value_ptr(projectionTransform));
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadMatrixf(glm::value_ptr(viewTransform));

  // The outline is coplanar with the surface it marks, so pull it toward the
  // viewer to keep it from fighting that surface for the same pixels.
  glEnable(GL_POLYGON_OFFSET_LINE);
  glPolygonOffset(-1.0f, -1.0f);
  glLineWidth(highlightLineWidth);

  glColor3f(1.0f, 1.0f, 0.0f);
  glBegin(GL_LINES);
  for (auto const& triangle : triangles) {
    for (size_t i = 0; i < triangle.vertices.size(); ++i) {
      submitVertex(triangle.vertices[i]);
      submitVertex(triangle.vertices[(i + 1) % triangle.vertices.size()]);
    }
  }
  glEnd();

  glLineWidth(1.0f);
  glPolygonOffset(0.0f, 0.0f);
  glDisable(GL_POLYGON_OFFSET_LINE);

  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
}

}  // namespace editor
