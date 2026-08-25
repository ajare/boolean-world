#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <GL/glew.h>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat3x3.hpp>
#pragma warning(pop)

#include <mpp/mesh/MeshSpecification.h>
#include <mpp/program/Parser.h>

#include <spdlog/spdlog.h>
#include <utils/Image.h>

#include "PreviewMaterialProgram.h"

extern spdlog::logger* gLogger;

namespace editor {
namespace {

std::string readTextFile(std::string const& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open " + path);
  }
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

// Matches the four attributes world.vert's @In(...) calls reference; the
// identifiers below are chosen by this call, not read back from anywhere -
// they only need to agree with the names used at draw time in Preview3D.cpp.
mpp::mesh::MeshSpecification buildMeshSpecification() {
  mpp::mesh::MeshSpecification spec(mpp::mesh::Primitive::Type::Triangles);
  auto* layout = spec.createVertexBufferAttributeLayout(false);
  layout->createAttribute(
      mpp::mesh::Vertex::Component::Position3, "POSITION",
      mpp::mesh::Vertex::DataType::Float, false);
  layout->createAttribute(
      mpp::mesh::Vertex::Component::Normal3, "NORMAL",
      mpp::mesh::Vertex::DataType::Float, false);
  layout->createAttribute(
      mpp::mesh::Vertex::Component::TexCoord2, "TEXCOORDS",
      mpp::mesh::Vertex::DataType::Float, false);
  layout->createAttribute(
      mpp::mesh::Vertex::Component::Colour4, "COLOUR",
      mpp::mesh::Vertex::DataType::Float, false);
  return spec;
}

uint32_t compileStage(GLenum stage, std::string const& source) {
  auto shader = glCreateShader(stage);
  auto const* sourcePtr = source.c_str();
  glShaderSource(shader, 1, &sourcePtr, nullptr);
  glCompileShader(shader);

  GLint status = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status == GL_FALSE) {
    GLint logLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(std::max(logLength, 1), '\0');
    glGetShaderInfoLog(shader, logLength, nullptr, log.data());
    glDeleteShader(shader);
    throw std::runtime_error("Preview material shader stage failed: " + log);
  }
  return shader;
}

}  // namespace

PreviewMaterialProgram::PreviewMaterialProgram(std::string fragmentShaderPath)
    : mFragmentShaderPath(std::move(fragmentShaderPath)) {}

PreviewMaterialProgram::~PreviewMaterialProgram() {
  if (mProgram) {
    glDeleteProgram(mProgram);
  }
  if (mCameraFrameUbo) {
    glDeleteBuffers(1, &mCameraFrameUbo);
  }
  if (mVertexBuffer) {
    glDeleteBuffers(1, &mVertexBuffer);
  }
  if (mVertexArray) {
    glDeleteVertexArrays(1, &mVertexArray);
  }
  if (mTexture) {
    glDeleteTextures(1, &mTexture);
  }
}

bool PreviewMaterialProgram::ensureReady() {
  if (mInitAttempted) {
    return mReady;
  }
  mInitAttempted = true;

  try {
    mReady = compile() && loadTexture();
  } catch (std::exception const& error) {
    gLogger->error(
        "3D preview: failed to set up the material shader: {}", error.what());
    mReady = false;
  }
  return mReady;
}

bool PreviewMaterialProgram::compile() {
  auto vertexSource = readTextFile("shaders/world.vert");
  auto fragmentSource = readTextFile(mFragmentShaderPath);

  mpp::program::Parser parser("PreviewMaterial");
  parser.setMeshSpecification(buildMeshSpecification());
  parser.setVertexSource(vertexSource);
  parser.setFragmentSource(fragmentSource);
  parser.build({"Texture"});

  for (auto const& warning : parser.getWarnings()) {
    gLogger->warn("3D preview material shader: {}", warning);
  }
  // Reported but not treated as fatal. These shaders are authored for, and
  // consumed by, the app module, whose pipeline never inspects this error
  // list - so a shader the game renders fine must not black out the preview.
  // The generated GLSL is the real arbiter, and it is compiled below.
  for (auto const& error : parser.getErrors()) {
    gLogger->error("3D preview material shader: {}", error);
  }

  auto vertexStage =
      compileStage(GL_VERTEX_SHADER, parser.getGeneratedVertexSource());
  auto fragmentStage =
      compileStage(GL_FRAGMENT_SHADER, parser.getGeneratedFragmentSource());

  mProgram = glCreateProgram();
  glAttachShader(mProgram, vertexStage);
  glAttachShader(mProgram, fragmentStage);
  glLinkProgram(mProgram);
  glDeleteShader(vertexStage);
  glDeleteShader(fragmentStage);

  GLint linkStatus = GL_FALSE;
  glGetProgramiv(mProgram, GL_LINK_STATUS, &linkStatus);
  if (linkStatus == GL_FALSE) {
    GLint logLength = 0;
    glGetProgramiv(mProgram, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(std::max(logLength, 1), '\0');
    glGetProgramInfoLog(mProgram, logLength, nullptr, log.data());
    gLogger->error("3D preview material shader failed to link: {}", log);
    glDeleteProgram(mProgram);
    mProgram = 0;
    return false;
  }

  mUniformViewDistance = glGetUniformLocation(mProgram, "_mpp_u_VIEW_DISTANCE_");
  mUniformGlobalTime = glGetUniformLocation(mProgram, "_mpp_u_GLOBAL_TIME_");
  mUniformPixelSize = glGetUniformLocation(mProgram, "_mpp_u_PIXEL_SIZE_");
  mUniformFarGridSize = glGetUniformLocation(mProgram, "_mpp_u_FAR_GRID_SIZE_");
  mUniformPlayerPosition =
      glGetUniformLocation(mProgram, "_mpp_u_PLAYER_POSITION_");
  mUniformLightPosition =
      glGetUniformLocation(mProgram, "_mpp_u_LIGHT_POSITION_");
  mUniformMaterialScale =
      glGetUniformLocation(mProgram, "_mpp_u_MATERIAL_SCALE_");
  mUniformHexagonRadius =
      glGetUniformLocation(mProgram, "_mpp_u_HEXAGON_RADIUS_");
  mUniformHexagonDepth = glGetUniformLocation(mProgram, "_mpp_u_HEXAGON_DEPTH_");
  mUniformTileDepthVariationFactor = glGetUniformLocation(
      mProgram, "_mpp_u_TILE_DEPTH_VARIATION_FACTOR_");
  mUniformRunningBondWidthPercent = glGetUniformLocation(
      mProgram, "_mpp_u_RUNNING_BOND_WIDTH_PERCENT_");
  mUniformRunningBondOffsetPercent = glGetUniformLocation(
      mProgram, "_mpp_u_RUNNING_BOND_OFFSET_PERCENT_");
  mUniformVoronoiRoundedEdgeFactor = glGetUniformLocation(
      mProgram, "_mpp_u_VORONOI_ROUNDED_EDGE_FACTOR_");
  mUniformSecondaryMaterialIndex = glGetUniformLocation(
      mProgram, "_mpp_u_SECONDARY_MATERIAL_INDEX_");
  mUniformUseSecondaryMaterial = glGetUniformLocation(
      mProgram, "_mpp_u_USE_SECONDARY_MATERIAL_");
  mUniformFloorPattern = glGetUniformLocation(mProgram, "_mpp_u_FLOOR_PATTERN_");
  mUniformMaterialIndex =
      glGetUniformLocation(mProgram, "_mpp_u_MATERIAL_INDEX_");
  mUniformMaterialParams =
      glGetUniformLocation(mProgram, "_mpp_u_MATERIAL_PARAMS_");
  mUniformViewPos = glGetUniformLocation(mProgram, "_mpp_u_viewPos_");
  mUniformTexture = glGetUniformLocation(mProgram, "_mpp_t_TEX1_");
  mUniformModel = glGetUniformLocation(mProgram, "_mpp_u_model_");
  mUniformModelCameraProjection =
      glGetUniformLocation(mProgram, "_mpp_u_modelCameraProjection_");
  mUniformNormalMatrix = glGetUniformLocation(mProgram, "_mpp_u_normal_");

  // Own vertex array and buffer. The attribute layout is baked into the VAO
  // once here, so drawing only has to bind it and upload.
  glGenVertexArrays(1, &mVertexArray);
  glGenBuffers(1, &mVertexBuffer);
  glBindVertexArray(mVertexArray);
  glBindBuffer(GL_ARRAY_BUFFER, mVertexBuffer);
  auto stride = (GLsizei)sizeof(PreviewGpuVertex);
  glEnableVertexAttribArray(kPositionAttrib);
  glVertexAttribPointer(
      kPositionAttrib, 3, GL_FLOAT, GL_FALSE, stride,
      (void const*)offsetof(PreviewGpuVertex, px));
  glEnableVertexAttribArray(kNormalAttrib);
  glVertexAttribPointer(
      kNormalAttrib, 3, GL_FLOAT, GL_FALSE, stride,
      (void const*)offsetof(PreviewGpuVertex, nx));
  glEnableVertexAttribArray(kTexCoordAttrib);
  glVertexAttribPointer(
      kTexCoordAttrib, 2, GL_FLOAT, GL_FALSE, stride,
      (void const*)offsetof(PreviewGpuVertex, u));
  glEnableVertexAttribArray(kColourAttrib);
  glVertexAttribPointer(
      kColourAttrib, 4, GL_FLOAT, GL_FALSE, stride,
      (void const*)offsetof(PreviewGpuVertex, r));
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  glGenBuffers(1, &mCameraFrameUbo);
  glBindBuffer(GL_UNIFORM_BUFFER, mCameraFrameUbo);
  // mat4 VIEW_MATRIX, PROJECTION_MATRIX, INVERSE_PROJECTION_MATRIX (64 bytes
  // each, std140) + vec4 VIEWPORT_SIZE, NEAR_FAR_TIME (16 bytes each).
  glBufferData(GL_UNIFORM_BUFFER, 224, nullptr, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
  // Matches the fixed `layout(std140, binding = 3)` in world_pbr.frag.
  glBindBufferBase(GL_UNIFORM_BUFFER, 3, mCameraFrameUbo);

  return true;
}

bool PreviewMaterialProgram::loadTexture() {
  utils::Image image;
  image.loadFromFile("shaders/floor5.png");
  if (!image.getData() || image.getWidth() == 0 || image.getHeight() == 0) {
    gLogger->error("3D preview: failed to load shaders/floor5.png");
    return false;
  }

  glGenTextures(1, &mTexture);
  glBindTexture(GL_TEXTURE_2D, mTexture);
  auto format = image.getBitsPerPixel() == 32 ? GL_RGBA : GL_RGB;
  glTexImage2D(
      GL_TEXTURE_2D, 0, format, (GLsizei)image.getWidth(),
      (GLsizei)image.getHeight(), 0, format, GL_UNSIGNED_BYTE,
      image.getData());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, 0);
  return true;
}

void PreviewMaterialProgram::begin(
    glm::mat4 const& viewMatrix,
    glm::mat4 const& projectionMatrix,
    glm::vec3 const& cameraPosition,
    glm::vec3 const& lightPosition,
    float globalTime) {
  glUseProgram(mProgram);
  glBindVertexArray(mVertexArray);

  struct CameraFrame {
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 inverseProjection;
    glm::vec4 viewportSize;
    glm::vec4 nearFarTime;
  } cameraFrame{
      viewMatrix, projectionMatrix, glm::mat4(1.0f), glm::vec4(0.0f),
      glm::vec4(0.0f)};

  glBindBuffer(GL_UNIFORM_BUFFER, mCameraFrameUbo);
  glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(CameraFrame), &cameraFrame);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
  glBindBufferBase(GL_UNIFORM_BUFFER, 3, mCameraFrameUbo);

  glUniform1f(mUniformViewDistance, BW_PLAYER_VIEW_DISTANCE);
  glUniform1f(mUniformGlobalTime, globalTime);
  glUniform1f(mUniformPixelSize, 1.0f / 32.0f);
  glUniform1f(mUniformFarGridSize, 0.5f);
  glUniform3fv(mUniformPlayerPosition, 1, glm::value_ptr(cameraPosition));
  glUniform3fv(mUniformLightPosition, 1, glm::value_ptr(lightPosition));
  glUniform1f(mUniformMaterialScale, 32.0f);
  glUniform1f(mUniformHexagonRadius, 16.0f);
  glUniform1f(mUniformHexagonDepth, 0.5f);
  glUniform1f(mUniformTileDepthVariationFactor, 0.1f);
  glUniform1f(mUniformRunningBondWidthPercent, 50.0f);
  glUniform1f(mUniformRunningBondOffsetPercent, 50.0f);
  glUniform1f(mUniformVoronoiRoundedEdgeFactor, 0.25f);
  glUniform1i(mUniformSecondaryMaterialIndex, -1);
  glUniform1i(mUniformUseSecondaryMaterial, 0);
  // No FloorPatternOptions source exists for this raw preview - keep pattern
  // embossing off rather than guess at hexagon/running-bond tiling.
  glUniform1i(mUniformFloorPattern, 0);
  glUniform3fv(mUniformViewPos, 1, glm::value_ptr(cameraPosition));

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, mTexture);
  glUniform1i(mUniformTexture, 0);

  // Preview geometry is already baked into world space - model is always
  // the identity transform.
  glm::mat4 model(1.0f);
  glm::mat4 modelCameraProjection = projectionMatrix * viewMatrix * model;
  glm::mat3 normalMatrix(1.0f);
  glUniformMatrix4fv(mUniformModel, 1, GL_FALSE, glm::value_ptr(model));
  glUniformMatrix4fv(
      mUniformModelCameraProjection, 1, GL_FALSE,
      glm::value_ptr(modelCameraProjection));
  glUniformMatrix3fv(
      mUniformNormalMatrix, 1, GL_FALSE, glm::value_ptr(normalMatrix));
}

void PreviewMaterialProgram::setMaterial(
    uint32_t materialIndex, std::array<float, BW_MATERIAL_PARAMS_MAX> const& params) {
  glUniform1i(mUniformMaterialIndex, (int32_t)materialIndex);
  glUniform1fv(
      mUniformMaterialParams, (GLsizei)params.size(), params.data());
}

void PreviewMaterialProgram::draw(std::vector<PreviewGpuVertex> const& vertices) {
  if (vertices.empty()) {
    return;
  }
  glBindBuffer(GL_ARRAY_BUFFER, mVertexBuffer);
  glBufferData(
      GL_ARRAY_BUFFER,
      (GLsizeiptr)(vertices.size() * sizeof(PreviewGpuVertex)),
      vertices.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertices.size());
}

void PreviewMaterialProgram::end() {
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glUseProgram(0);
}

}  // namespace editor
