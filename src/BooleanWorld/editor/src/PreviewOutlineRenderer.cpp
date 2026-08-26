#include "PreviewOutlineRenderer.h"

#include <stdexcept>
#include <string>

#include <GL/glew.h>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/gtc/type_ptr.hpp>
#pragma warning(pop)

namespace editor {
namespace {

// GLSL 130 to match the editor's GL 3.0 context - the same version the ImGui
// backend compiles against in this window.
constexpr char const* vertexShaderSource = R"(#version 130
in vec3 aPosition;
uniform mat4 uViewProjection;
void main() {
  gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)";

constexpr char const* fragmentShaderSource = R"(#version 130
uniform vec4 uColour;
out vec4 fragColour;
void main() {
  fragColour = uColour;
}
)";

// Wide enough to read against a busy wall. Drivers are free to clamp this to
// 1.0 - nothing here depends on the request being honoured.
constexpr float outlineWidth = 2.0f;

std::uint32_t compileShader(GLenum type, char const* source) {
  auto shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);

  GLint compiled{};
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    GLint length{};
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<size_t>(length > 0 ? length : 1), '\0');
    glGetShaderInfoLog(shader, length, nullptr, log.data());
    glDeleteShader(shader);
    throw std::runtime_error(
        "PreviewOutlineRenderer: shader would not compile: " + log);
  }
  return shader;
}

}  // namespace

PreviewOutlineRenderer::PreviewOutlineRenderer() {
  auto vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
  std::uint32_t fragmentShader{};
  try {
    fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
  } catch (...) {
    glDeleteShader(vertexShader);
    throw;
  }

  mProgram = glCreateProgram();
  glAttachShader(mProgram, vertexShader);
  glAttachShader(mProgram, fragmentShader);
  glBindAttribLocation(mProgram, 0, "aPosition");
  glLinkProgram(mProgram);
  // Attached shaders are kept alive by the program until it links; deleting
  // them here means the program owns the only reference from now on.
  glDetachShader(mProgram, vertexShader);
  glDetachShader(mProgram, fragmentShader);
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);

  GLint linked{};
  glGetProgramiv(mProgram, GL_LINK_STATUS, &linked);
  if (!linked) {
    GLint length{};
    glGetProgramiv(mProgram, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<size_t>(length > 0 ? length : 1), '\0');
    glGetProgramInfoLog(mProgram, length, nullptr, log.data());
    glDeleteProgram(mProgram);
    mProgram = 0;
    throw std::runtime_error(
        "PreviewOutlineRenderer: program would not link: " + log);
  }

  mViewProjectionUniform = glGetUniformLocation(mProgram, "uViewProjection");
  mColourUniform = glGetUniformLocation(mProgram, "uColour");

  glGenVertexArrays(1, &mVertexArray);
  glGenBuffers(1, &mVertexBuffer);
  glGenFramebuffers(1, &mFrameBuffer);
}

PreviewOutlineRenderer::~PreviewOutlineRenderer() {
  if (mFrameBuffer) glDeleteFramebuffers(1, &mFrameBuffer);
  if (mVertexBuffer) glDeleteBuffers(1, &mVertexBuffer);
  if (mVertexArray) glDeleteVertexArrays(1, &mVertexArray);
  if (mProgram) glDeleteProgram(mProgram);
}

void PreviewOutlineRenderer::render(
    std::uint32_t targetTextureId,
    std::size_t width,
    std::size_t height,
    glm::mat4 const& viewProjection,
    std::vector<PreviewOutline> const& outlines) {
  if (!mProgram || !targetTextureId || !width || !height) {
    return;
  }

  // One upload for the lot; each outline is then one draw over its own range,
  // which is what keeps a second colour from costing a second buffer.
  std::vector<glm::vec3> vertices;
  for (auto const& outline : outlines) {
    vertices.insert(
        vertices.end(), outline.segments.begin(), outline.segments.end());
  }
  if (vertices.empty()) {
    return;
  }

  GLint previousFrameBuffer{};
  GLint previousProgram{};
  GLint previousVertexArray{};
  GLint previousArrayBuffer{};
  GLint previousViewport[4]{};
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFrameBuffer);
  glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
  glGetIntegerv(GL_VIEWPORT, previousViewport);
  auto previousDepthTest = glIsEnabled(GL_DEPTH_TEST);
  auto previousBlend = glIsEnabled(GL_BLEND);
  auto previousScissor = glIsEnabled(GL_SCISSOR_TEST);

  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mFrameBuffer);
  // Reattached per draw: a resize replaces the pipeline's output texture, and
  // this is cheaper than tracking which texture is current.
  glFramebufferTexture2D(
      GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
      targetTextureId, 0);

  if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    // The whole point of the pass: the outline is drawn over the finished
    // image, so a border that runs behind the geometry it belongs to still
    // reads as one closed shape.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);

    glBindVertexArray(mVertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, mVertexBuffer);
    auto const bytes = vertices.size() * sizeof(glm::vec3);
    if (vertices.size() > mVertexCapacity) {
      glBufferData(GL_ARRAY_BUFFER, bytes, vertices.data(), GL_STREAM_DRAW);
      mVertexCapacity = vertices.size();
    } else {
      glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, vertices.data());
    }
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);

    glUseProgram(mProgram);
    glUniformMatrix4fv(
        mViewProjectionUniform, 1, GL_FALSE, glm::value_ptr(viewProjection));

    glLineWidth(outlineWidth);
    GLint first = 0;
    for (auto const& outline : outlines) {
      if (outline.segments.empty()) {
        continue;
      }
      glUniform4fv(mColourUniform, 1, glm::value_ptr(outline.colour));
      glDrawArrays(
          GL_LINES, first, static_cast<GLsizei>(outline.segments.size()));
      first += static_cast<GLint>(outline.segments.size());
    }
    glLineWidth(1.0f);
  }

  // Detached so this framebuffer never keeps a pipeline texture alive past a
  // resize, and so nothing else can draw into it by accident.
  glFramebufferTexture2D(
      GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);

  glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousArrayBuffer));
  glBindVertexArray(static_cast<GLuint>(previousVertexArray));
  glUseProgram(static_cast<GLuint>(previousProgram));
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousFrameBuffer));
  glViewport(
      previousViewport[0], previousViewport[1], previousViewport[2],
      previousViewport[3]);
  if (previousDepthTest) glEnable(GL_DEPTH_TEST);
  if (previousBlend) glEnable(GL_BLEND);
  if (previousScissor) glEnable(GL_SCISSOR_TEST);
}

}  // namespace editor
