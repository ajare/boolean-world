#include <mpp/ProgrammaticMaterialStream.h>

#include <utils/StringUtils.h>

#include "willpower/common/Exceptions.h"

#include "willpower/viz/GeometryMeshRenderer.h"

const std::string VertexShaderTemplate =
    R"(
@@Version

void main()
{
	@Out(vec4 TEXCOORDS) = @In(TEXCOORDS);
	@Out(vec4 COLOUR) = @Vec4(@In(COLOUR));
	@Out(vec2 WEIGHTS) = @Vec2(@In(POSITION).zw);

	vec4 transVertex = @MCPMatrix * vec4(@In(POSITION).xy, 0, 1);
	vec2 centredPos = vec2(transVertex.x - @HalfWindowSize.x, transVertex.y - @HalfWindowSize.y);
	gl_Position = vec4(centredPos / @HalfWindowSize, 0, 1);
}
)";

const std::string FragmentShaderTemplate =
    R"(
@@Version

@@Uniform(vec4 DIFFUSE);
@@Texture(sampler2D TEX1);
@@Texture(sampler2D TEX2);

void main()
{
	vec2 weights = @Vec2(@In(WEIGHTS));
	vec4 colour = @Vec4(@In(COLOUR));
    colour *= @Uniform(DIFFUSE);

	vec2 tc0 = @In(TEXCOORDS).st;
	vec2 tc1 = @In(TEXCOORDS).pq;

	@Out(vec4 COLOUR) = (texture(@Texture(TEX1), tc0) * weights.x + texture(@Texture(TEX2), tc1) * weights.y) * colour;
}
)";

namespace WP_NAMESPACE {
namespace viz {

using namespace std;
using namespace wp;

GeometryMeshRenderer::GeometryMeshRenderer(string const& name, shared_ptr<geometry::Mesh> mesh, GridOptions const& gridOptions, size_t indexWidth, mpp::ResourceManager* renderResourceMgr)
    : StaticRenderer(name, "GeometryMeshRenderer", gridOptions, indexWidth, renderResourceMgr), mMesh(mesh) {
}

void GeometryMeshRenderer::getExtents(Vector2& minExtent, Vector2& maxExtent) {
  mMesh->getExtents(minExtent, maxExtent);
}

void GeometryMeshRenderer::createMeshSpecifications() {
  // Spec for background
  mpp::mesh::MeshSpecification backgroundMeshSpec(mpp::mesh::Primitive::Type::Triangles);

  mpp::mesh::VertexBufferAttributeLayout* attribLayout = backgroundMeshSpec.createVertexBufferAttributeLayout(false);
  attribLayout->createAttribute(mpp::mesh::Vertex::Component::Position4, mpp::mesh::Vertex::DataType::Float, false);
  attribLayout->createAttribute(mpp::mesh::Vertex::Component::TexCoord4, mpp::mesh::Vertex::DataType::Float, false);
  attribLayout->createAttribute(mpp::mesh::Vertex::Component::Colour4, mpp::mesh::Vertex::DataType::UnsignedByte, true);
  backgroundMeshSpec.setStorageType(mpp::mesh::VertexBufferStorageType::Static);
  backgroundMeshSpec.setIndexedVertices(false);

  mMeshSpecifications["Background"] = backgroundMeshSpec;

  // Spec for displaying polygon triangles
  mpp::mesh::MeshSpecification triangleMeshSpec(mpp::mesh::Primitive::Type::Triangles);

  attribLayout = triangleMeshSpec.createVertexBufferAttributeLayout(false);
  attribLayout->createAttribute(mpp::mesh::Vertex::Component::Position2, mpp::mesh::Vertex::DataType::Float, false);
  attribLayout->createAttribute(mpp::mesh::Vertex::Component::TexCoord2, mpp::mesh::Vertex::DataType::Float, false);
  attribLayout->createAttribute(mpp::mesh::Vertex::Component::Colour4, mpp::mesh::Vertex::DataType::UnsignedByte, true);
  triangleMeshSpec.setStorageType(mpp::mesh::VertexBufferStorageType::Static);
  triangleMeshSpec.setIndexedVertices(false);

  mMeshSpecifications["Triangles"] = triangleMeshSpec;

  mpp::mesh::MeshSpecification lineMeshSpec(mpp::mesh::Primitive::Type::Lines);

  attribLayout = lineMeshSpec.createVertexBufferAttributeLayout(false);
  attribLayout->createAttribute(mpp::mesh::Vertex::Component::Position2, mpp::mesh::Vertex::DataType::Float, false);
  attribLayout->createAttribute(mpp::mesh::Vertex::Component::Colour4, mpp::mesh::Vertex::DataType::UnsignedByte, true);
  lineMeshSpec.setStorageType(mpp::mesh::VertexBufferStorageType::Static);
  lineMeshSpec.setIndexedVertices(false);

  mMeshSpecifications["Lines"] = lineMeshSpec;

  mpp::mesh::MeshSpecification vertexMeshSpec(mpp::mesh::Primitive::Type::Points);

  attribLayout = vertexMeshSpec.createVertexBufferAttributeLayout(false);
  attribLayout->createAttribute(mpp::mesh::Vertex::Component::Position2, mpp::mesh::Vertex::DataType::Float, false);
  attribLayout->createAttribute(mpp::mesh::Vertex::Component::Colour4, mpp::mesh::Vertex::DataType::UnsignedByte, true);
  vertexMeshSpec.setStorageType(mpp::mesh::VertexBufferStorageType::Static);
  vertexMeshSpec.setIndexedVertices(false);

  mMeshSpecifications["Vertices"] = vertexMeshSpec;
}

vector<string> GeometryMeshRenderer::getPolygonTextures(uint32_t polygonIndex, uint32_t numTextures) const {
  vector<string> textures;
  mMesh->getPolygonTexturesAttribute(polygonIndex, textures);

  while (textures.size() > numTextures) {
    textures.pop_back();
  }

  while (textures.size() < numTextures) {
    textures.push_back("__mpp_tex_none__");
  }

  return textures;
}

string GeometryMeshRenderer::getPolygonMaterialLookupKey(uint32_t polygonIndex) const {
  string lookupKey;

  string matName;
  mMesh->getPolygonMaterialAttribute(polygonIndex, matName);

  if (matName != "") {
    lookupKey = matName;
  } else {
    string progName;
    mMesh->getPolygonProgramAttribute(polygonIndex, progName);

    if (progName != "") {
      lookupKey = progName + "_";
    } else {
      lookupKey = "GeometryMeshRenderer-DefaultProgram_";
    }

    vector<string> textures;
    mMesh->getPolygonTexturesAttribute(polygonIndex, textures);

    lookupKey += utils::StringUtils::join(textures.begin(), textures.end(), "_");
  }

  return lookupKey;
}

string GeometryMeshRenderer::getPolygonMaterialName(uint32_t polygonIndex) const {
  return getName() + "_Background_" + getPolygonMaterialLookupKey(polygonIndex);
}

string GeometryMeshRenderer::getPolygonMeshNamePrefix(uint32_t polygonIndex) const {
  WP_UNUSED(polygonIndex);
  return "M";
}

void GeometryMeshRenderer::createMaterials(mpp::ResourceManager* resourceMgr) {
  string name = getName();
  string type = getType();

  // Background: go through all mesh polygons, and get/create the materials.
  uint32_t polygonIndex = mMesh->getFirstPolygonIndex();
  while (!mMesh->polygonIndexIterationFinished(polygonIndex)) {
    auto const& polygon = mMesh->getPolygon(polygonIndex);
    if (!polygon.isHole()) {
      // If the polygon has a material name, then we need to add it once
      // into the static renderer.  If it doesn't, then we look for a
      // program.  If the program is specified, we use that to create the
      // material.
      string matName;
      mMesh->getPolygonMaterialAttribute(polygonIndex, matName);

      if (matName == "") {
        string progName;
        mMesh->getPolygonProgramAttribute(polygonIndex, progName);

        mpp::ResourcePtr program;
        uint32_t numTextures;

        if (progName != "") {
          program = resourceMgr->getResource(progName);
          numTextures = static_cast<mpp::Program*>(program.get())->getNumSamplers();
        } else {
          program = resourceMgr->getDefault2dProgram(
              VertexShaderTemplate,
              FragmentShaderTemplate,
              mMeshSpecifications["Background"],
              0,
              false);

          numTextures = 2;
        }

        matName = getPolygonMaterialName(polygonIndex);
        if (!resourceMgr->getResource(matName, true)) {
          auto matStream = make_shared<mpp::ProgrammaticMaterialStream>(resourceMgr);

          matStream->setProgram(program->getName());

          vector<string> textures = getPolygonTextures(polygonIndex, numTextures);

          matStream->setTexture("TEX1", textures[0]);
          matStream->setTexture("TEX2", textures[1]);

          resourceMgr->declareResource(matName, matStream);
        }
      }

      addMaterialResource(matName, resourceMgr->getResource(matName));
    }

    polygonIndex = mMesh->getNextPolygonIndex(polygonIndex);
  }

  // Triangles
  auto programResource = resourceMgr->getDefault2dProgram(
      mMeshSpecifications["Triangles"],
      MPP_PROGRAM_TAGS_PRIM_TRIANGLES | MPP_PROGRAM_TAGS_TEXTURE | MPP_PROGRAM_TAGS_DIFFUSE,
      false,
      type);

  auto matStream = make_shared<mpp::ProgrammaticMaterialStream>(resourceMgr);

  matStream->setProgram(programResource->getName());
  matStream->setTexture("TEX1", "__mpp_tex_none__");

  addMaterialResource("Triangles", resourceMgr->declareResource(name + "_Triangles", matStream).first);

  // Lines
  programResource = resourceMgr->getDefault2dProgram(
      mMeshSpecifications["Lines"],
      MPP_PROGRAM_TAGS_PRIM_LINES | MPP_PROGRAM_TAGS_DIFFUSE,
      false,
      type);

  matStream = make_shared<mpp::ProgrammaticMaterialStream>(resourceMgr);

  matStream->setProgram(programResource->getName());

  addMaterialResource("Lines", resourceMgr->declareResource(name + "_Lines", matStream).first);

  // Vertices
  uint32_t flags{MPP_PROGRAM_TAGS_TEXTURE | MPP_PROGRAM_TAGS_DIFFUSE};
  if (mMeshSpecifications["Vertices"].getPrimitiveType() == mpp::mesh::Primitive::Type::Points) {
    flags |= MPP_PROGRAM_TAGS_PRIM_POINTS;
  } else {
    flags |= MPP_PROGRAM_TAGS_PRIM_TRIANGLES;
  }

  programResource = resourceMgr->getDefault2dProgram(
      mMeshSpecifications["Vertices"],
      flags,
      false,
      type);

  matStream = make_shared<mpp::ProgrammaticMaterialStream>(resourceMgr);

  matStream->setProgram(programResource->getName());
  matStream->setTexture("TEX1", "__mpp_tex_none__");

  addMaterialResource("Vertices", resourceMgr->declareResource(name + "_Vertices", matStream).first);
}

void GeometryMeshRenderer::createMeshes(mpp::ProgrammaticModelStream* stream, mpp::ResourceManager* resourceMgr) {
  int gridDimX, gridDimY;
  if (usingLookupGrid()) {
    gridDimX = mLookupGrid->getCellDimensionX();
    gridDimY = mLookupGrid->getCellDimensionY();
  } else {
    gridDimX = 1;
    gridDimY = 1;
  }

  for (int y = 0; y < gridDimY; ++y) {
    for (int x = 0; x < gridDimX; ++x) {
      BoundingBox cellBounds;
      if (usingLookupGrid()) {
        // Get cell clipping rect
        Vector2 minExtent, maxExtent;
        mLookupGrid->getCellExtents(x, y, minExtent, maxExtent);

        cellBounds.setPosition(minExtent);
        cellBounds.setSize(maxExtent - minExtent);
      }

      // Clip each polygon against it.
      uint32_t polygonIndex = mMesh->getFirstPolygonIndex();
      while (!mMesh->polygonIndexIterationFinished(polygonIndex)) {
        auto const& polygon = mMesh->getPolygon(polygonIndex);
        if (!polygon.isHole()) {
          // Bounds check
          auto polyBounds = polygon.getBoundingBox();
          if (!usingLookupGrid() || cellBounds.intersectsBoundingObject(&polyBounds)) {
            for (size_t i = 0; i < polygon.getTriangulationTriangleCount(); ++i) {
              uint32_t i0, i1, i2;
              polygon.getTriangulationVertexIndices(i, i0, i1, i2);

              auto const& v0 = mMesh->getVertex(i0);
              auto const& v1 = mMesh->getVertex(i1);
              auto const& v2 = mMesh->getVertex(i2);

              // Position
              Vector2 p[3];
              p[0] = v0.getPosition();
              p[1] = v1.getPosition();
              p[2] = v2.getPosition();

              // Test triangle against the grid
              switch (getGridCellStrategy()) {
                case GridOptions::CellStrategy::Intersects:
                  if (!cellBounds.intersectsTriangle(p[0], p[1], p[2])) {
                    continue;
                  }
                  break;

                case GridOptions::CellStrategy::CentreInside:
                  if (!cellBounds.pointInside((p[0] + p[1] + p[2]) / 3.0f)) {
                    continue;
                  }
                  break;
              }

              // Get material
              string matName;
              mMesh->getPolygonMaterialAttribute(polygonIndex, matName);

              if (matName == "") {
                matName = getPolygonMaterialName(polygonIndex);
              }

              // Create mesh
              auto meshNamePrefix = getPolygonMeshNamePrefix(polygonIndex);
              string meshName = STR_FORMAT("Background_{}_{}_{}_{}", meshNamePrefix, matName, x, y);

              auto res = mBackgroundMeshes.insert(pair(meshName, BackgroundMeshData()));
              auto& data = res.first->second;
              auto exists = !res.second;

              if (!exists) {
                data.material = resourceMgr->getResource(matName);
                data.numVertices = 0;
                data.meshId = stream->createMesh(meshName, mMeshSpecifications["Background"], matName, (int)mIndexWidth);

                if (usingLookupGrid()) {
                  mBackgroundMeshLookup[(uint32_t)data.meshId] = meshName;

                  BoundingBox bbox = cellBounds;
                  bbox.expand(-0.9f);
                  mLookupGrid->addItem((uint32_t)data.meshId, bbox);
                }
              }

              // Texcoord set 1
              float t00[2] = {0.0f, 0.0f};
              float t01[2] = {0.0f, 0.0f};
              float t02[2] = {0.0f, 0.0f};

              mMesh->getPolygonVertexUvAttribute(polygonIndex, i0, 0, t00[0], t00[1]);
              mMesh->getPolygonVertexUvAttribute(polygonIndex, i1, 0, t01[0], t01[1]);
              mMesh->getPolygonVertexUvAttribute(polygonIndex, i2, 0, t02[0], t02[1]);

              // Texcoord set 2
              float t10[2] = {0.0f, 0.0f};
              float t11[2] = {0.0f, 0.0f};
              float t12[2] = {0.0f, 0.0f};

              mMesh->getPolygonVertexUvAttribute(polygonIndex, i0, 1, t10[0], t10[1]);
              mMesh->getPolygonVertexUvAttribute(polygonIndex, i1, 1, t11[0], t11[1]);
              mMesh->getPolygonVertexUvAttribute(polygonIndex, i2, 1, t12[0], t12[1]);

              // Texcoord weights
              float w0[2] = {0.5f, 0.5f};
              float w1[2] = {0.5f, 0.5f};
              float w2[2] = {0.5f, 0.5f};

              mMesh->getPolygonVertexUvWeightAttribute(polygonIndex, i0, 0, w0[0]);
              mMesh->getPolygonVertexUvWeightAttribute(polygonIndex, i1, 0, w1[0]);
              mMesh->getPolygonVertexUvWeightAttribute(polygonIndex, i2, 0, w2[0]);
              mMesh->getPolygonVertexUvWeightAttribute(polygonIndex, i0, 1, w0[1]);
              mMesh->getPolygonVertexUvWeightAttribute(polygonIndex, i1, 1, w1[1]);
              mMesh->getPolygonVertexUvWeightAttribute(polygonIndex, i2, 1, w2[1]);

              // Colour
              float c0[4] = {1.0f, 1.0f, 1.0f, 1.0f};
              float c1[4] = {1.0f, 1.0f, 1.0f, 1.0f};
              float c2[4] = {1.0f, 1.0f, 1.0f, 1.0f};

              wp::geometry::UserAttributePolygonColourType colourType;
              mMesh->getPolygonColourType(polygonIndex, colourType);

              switch (colourType) {
                case wp::geometry::UserAttributePolygonColourType::Default:
                  break;

                case wp::geometry::UserAttributePolygonColourType::Polygon:
                  mMesh->getPolygonRgbaAttribute(polygonIndex, c0[0], c0[1], c0[2], c0[3]);
                  mMesh->getPolygonRgbaAttribute(polygonIndex, c1[0], c1[1], c0[2], c1[3]);
                  mMesh->getPolygonRgbaAttribute(polygonIndex, c2[0], c2[1], c0[2], c2[3]);
                  break;

                case wp::geometry::UserAttributePolygonColourType::Vertex:
                  mMesh->getVertexRgbaAttribute(i0, c0[0], c0[1], c0[2], c0[3]);
                  mMesh->getVertexRgbaAttribute(i1, c1[0], c1[1], c0[2], c1[3]);
                  mMesh->getVertexRgbaAttribute(i2, c2[0], c2[1], c0[2], c2[3]);
                  break;

                case wp::geometry::UserAttributePolygonColourType::PolygonVertex:
                  mMesh->getPolygonVertexRgbaAttribute(polygonIndex, i0, c0[0], c0[1], c0[2], c0[3]);
                  mMesh->getPolygonVertexRgbaAttribute(polygonIndex, i1, c1[0], c1[1], c0[2], c1[3]);
                  mMesh->getPolygonVertexRgbaAttribute(polygonIndex, i2, c2[0], c2[1], c0[2], c2[3]);
                  break;

                default:
                  break;
              }

              // Vertex 1
              data.vertices.push_back(p[0].x);
              data.vertices.push_back(p[0].y);
              data.vertices.push_back(w0[0]);
              data.vertices.push_back(w0[1]);
              data.vertices.push_back(t00[0]);
              data.vertices.push_back(t00[1]);
              data.vertices.push_back(t10[0]);
              data.vertices.push_back(t10[1]);
              data.vertices.push_back(c0[0]);
              data.vertices.push_back(c0[1]);
              data.vertices.push_back(c0[2]);
              data.vertices.push_back(c0[3]);

              // Vertex 2
              data.vertices.push_back(p[1].x);
              data.vertices.push_back(p[1].y);
              data.vertices.push_back(w1[0]);
              data.vertices.push_back(w1[1]);
              data.vertices.push_back(t01[0]);
              data.vertices.push_back(t01[1]);
              data.vertices.push_back(t11[0]);
              data.vertices.push_back(t11[1]);
              data.vertices.push_back(c1[0]);
              data.vertices.push_back(c1[1]);
              data.vertices.push_back(c1[2]);
              data.vertices.push_back(c1[3]);

              // Vertex 3
              data.vertices.push_back(p[2].x);
              data.vertices.push_back(p[2].y);
              data.vertices.push_back(w2[0]);
              data.vertices.push_back(w2[1]);
              data.vertices.push_back(t02[0]);
              data.vertices.push_back(t02[1]);
              data.vertices.push_back(t12[0]);
              data.vertices.push_back(t12[1]);
              data.vertices.push_back(c2[0]);
              data.vertices.push_back(c2[1]);
              data.vertices.push_back(c2[2]);
              data.vertices.push_back(c2[3]);

              // Vertex count
              data.numVertices += 3;
            }
          }
        }

        polygonIndex = mMesh->getNextPolygonIndex(polygonIndex);
      }
    }
  }

  auto const& trianglesMeshSpec = mMeshSpecifications["Triangles"];
  auto const& linesMeshSpec = mMeshSpecifications["Lines"];
  auto const& verticesMeshSpec = mMeshSpecifications["Vertices"];

  // Create line meshes
  auto polygonEdgesMeshId = stream->createMesh("PolygonEdges", linesMeshSpec, mMaterialNames["Lines"], (int)mIndexWidth);
  auto borderMeshId = stream->createMesh("Border", linesMeshSpec, mMaterialNames["Lines"], (int)mIndexWidth);
  auto triangulationMeshId = stream->createMesh("Triangulation", linesMeshSpec, mMaterialNames["Lines"], (int)mIndexWidth);
  auto polygonEdgeNormalsMeshId = stream->createMesh("PolygonEdgeNormals", linesMeshSpec, mMaterialNames["Lines"], (int)mIndexWidth);
  auto polygonEdgeDirectionsMeshId = stream->createMesh("PolygonEdgeDirections", trianglesMeshSpec, mMaterialNames["Triangles"], (int)mIndexWidth);

  // Create point meshes
  auto vertexMeshId = stream->createMesh("Vertices", verticesMeshSpec, mMaterialNames["Vertices"], (int)mIndexWidth);

  // Add vertices.  Get the number of vertices first, by counting the number of triangles each polygon is
  // broken down into, then iterate over the polygons again to add the data.
  size_t numPolygonEdgesVertices{0}, numBorderVertices{0}, numTriangulationVertices{0};
  size_t numVertices = mMesh->getNumVertices(), numPolygonDirectionVertices{0};

  uint32_t polygonIndex = mMesh->getFirstPolygonIndex();
  while (!mMesh->polygonIndexIterationFinished(polygonIndex)) {
    auto const& polygon = mMesh->getPolygon(polygonIndex);
    if (!polygon.isHole()) {
      auto edgeSet = polygon.getEdgeIndexSet();

      numTriangulationVertices += polygon.getTriangulationTriangleCount() * 3 * 2;
      numPolygonEdgesVertices += edgeSet.size() * 2;

      for (auto edgeIndex : edgeSet) {
        auto const& edge = mMesh->getEdge(edgeIndex);
        if (edge.getConnectivity() == geometry::Edge::Connectivity::External) {
          numBorderVertices += 2;
        }
      }
    }

    polygonIndex = mMesh->getNextPolygonIndex(polygonIndex);
  }

  numPolygonDirectionVertices = 3 * numPolygonEdgesVertices / 2;

  // Create vertex data for background meshes
  for (auto& kvp : mBackgroundMeshes) {
    auto& data = kvp.second;
    data.vertexData = new mpp::mesh::VertexData(mMeshSpecifications["Background"], kvp.second.numVertices);

    for (uint32_t i = 0; i < data.numVertices; ++i) {
      auto offset = i * 12;

      Vector2 p(data.vertices[offset + 0], data.vertices[offset + 1]);
      Vector2 w(data.vertices[offset + 2], data.vertices[offset + 3]);
      Vector2 t0(data.vertices[offset + 4], data.vertices[offset + 5]);
      Vector2 t1(data.vertices[offset + 6], data.vertices[offset + 7]);

      addVertexData(data.vertexData, p, w, t0, t1, &(data.vertices[offset + 8]));
    }
  }

  mpp::mesh::VertexData polygonEdgeVertexData(linesMeshSpec, numPolygonEdgesVertices);
  mpp::mesh::VertexData borderVertexData(linesMeshSpec, numBorderVertices);
  mpp::mesh::VertexData triangulationVertexData(linesMeshSpec, numTriangulationVertices);
  mpp::mesh::VertexData vertexVertexData(verticesMeshSpec, numVertices);
  mpp::mesh::VertexData polygonEdgeNormalsVertexData(linesMeshSpec, numPolygonEdgesVertices);
  mpp::mesh::VertexData polygonEdgeDirectionsVertexData(trianglesMeshSpec, numPolygonDirectionVertices);

  // Build vertex render data
  uint32_t vertexIndex = mMesh->getFirstVertexIndex();
  while (!mMesh->vertexIndexIterationFinished(vertexIndex)) {
    auto const& vertex = mMesh->getVertex(vertexIndex);

    Vector2 v = vertex.getPosition();

    float t[2] = {0.0f, 0.0f};
    float c[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    // Get user data and see if that has overrides
    mMesh->getVertexUvAttribute(vertexIndex, 0, t[0], t[1]);
    mMesh->getVertexRgbaAttribute(vertexIndex, c[0], c[1], c[2], c[3]);

    addVertexData(&vertexVertexData, v, Vector2(t[0], t[1]), c);

    vertexIndex = mMesh->getNextVertexIndex(vertexIndex);
  }

  polygonIndex = mMesh->getFirstPolygonIndex();
  while (!mMesh->polygonIndexIterationFinished(polygonIndex)) {
    auto const& polygon = mMesh->getPolygon(polygonIndex);
    if (!polygon.isHole()) {
      // Build background triangle / triangulation line render data
      for (size_t i = 0; i < polygon.getTriangulationTriangleCount(); ++i) {
        uint32_t i0, i1, i2;
        polygon.getTriangulationVertexIndices(i, i0, i1, i2);

        auto const& v0 = mMesh->getVertex(i0);
        auto const& v1 = mMesh->getVertex(i1);
        auto const& v2 = mMesh->getVertex(i2);

        // Position
        Vector2 p0, p1, p2;
        p0 = v0.getPosition();
        p1 = v1.getPosition();
        p2 = v2.getPosition();

        // Triangulation edges
        float dc[4] = {1.0f, 1.0f, 1.0f, 1.0f};

        addVertexData(&triangulationVertexData, p0, Vector2::ZERO, dc);
        addVertexData(&triangulationVertexData, p1, Vector2::ZERO, dc);
        addVertexData(&triangulationVertexData, p1, Vector2::ZERO, dc);
        addVertexData(&triangulationVertexData, p2, Vector2::ZERO, dc);
        addVertexData(&triangulationVertexData, p2, Vector2::ZERO, dc);
        addVertexData(&triangulationVertexData, p0, Vector2::ZERO, dc);
      }

      // Build polygon edge render data
      auto edgeSet = polygon.getEdgeIndexSet();
      for (uint32_t edgeIndex : edgeSet) {
        auto const& edge = mMesh->getEdge(edgeIndex);

        Vector2 v0 = mMesh->getVertex(edge.getFirstVertex()).getPosition();
        Vector2 v1 = mMesh->getVertex(edge.getSecondVertex()).getPosition();
        float colour[4] = {1.0f, 1.0f, 1.0f, 1.0f};

        // Polygon edges
        addVertexData(&polygonEdgeVertexData, v0, {0.0f, 0.0f}, colour);
        addVertexData(&polygonEdgeVertexData, v1, {0.0f, 0.0f}, colour);

        // Border edges
        if (edge.getConnectivity() == geometry::Edge::Connectivity::External) {
          addVertexData(&borderVertexData, v0, {0.0f, 0.0f}, colour);
          addVertexData(&borderVertexData, v1, {0.0f, 0.0f}, colour);
        }
      }

      // Build edge normal & direction render data
      auto edgeIt = polygon.getFirstEdge();
      while (edgeIt != polygon.getEndEdge()) {
        auto n = polygon.getEdgeNormal(edgeIt);
        auto d = polygon.getEdgeDirection(edgeIt);
        auto c = polygon.getEdgeCentre(edgeIt);

        auto v0 = c + n * 10.0f;
        float colour[4] = {1.0f, 1.0f, 1.0f, 1.0f};

        // Edge normals
        addVertexData(&polygonEdgeNormalsVertexData, c, {0.0f, 0.0f}, colour);
        addVertexData(&polygonEdgeNormalsVertexData, v0, {0.0f, 0.0f}, colour);

        // Edge directions
        auto v1 = c + n * 5.0f;
        auto v2 = c - n * 5.0f;
        auto v3 = c + d * 5.0f;
        addVertexData(&polygonEdgeDirectionsVertexData, v1, {0.0f, 0.0f}, colour);
        addVertexData(&polygonEdgeDirectionsVertexData, v2, {0.0f, 0.0f}, colour);
        addVertexData(&polygonEdgeDirectionsVertexData, v3, {0.0f, 0.0f}, colour);

        edgeIt++;
      }
    }

    polygonIndex = mMesh->getNextPolygonIndex(polygonIndex);
  }

  for (auto const& kvp : mBackgroundMeshes) {
    stream->addVertexData(kvp.second.meshId, *kvp.second.vertexData);
  }

  stream->addVertexData(polygonEdgesMeshId, polygonEdgeVertexData);
  stream->addVertexData(borderMeshId, borderVertexData);
  stream->addVertexData(triangulationMeshId, triangulationVertexData);
  stream->addVertexData(vertexMeshId, vertexVertexData);
  stream->addVertexData(polygonEdgeNormalsMeshId, polygonEdgeNormalsVertexData);
  stream->addVertexData(polygonEdgeDirectionsMeshId, polygonEdgeDirectionsVertexData);

  // Tidy up
  for (auto& kvp : mBackgroundMeshes) {
    delete kvp.second.vertexData;
    kvp.second.vertexData = nullptr;
  }
}

RenderParams* GeometryMeshRenderer::createRenderParams(shared_ptr<mpp::ModelRenderParams> params) {
  return new GeometryMeshRenderParams(params);
}

void GeometryMeshRenderer::updateRenderParams() {
  auto renderParams = static_cast<wp::viz::GeometryMeshRenderParams*>(getParams().get());
  auto modelRenderParams = getModelRenderParams();

  for (auto& kvp : mBackgroundMeshes) {
    auto const& meshName = kvp.first;
    modelRenderParams->setMeshFlags(meshName, renderParams->getRenderBackground() ? mpp::ModelRenderParams::Flag_Visible : 0);
  }

  modelRenderParams->setMeshFlags("PolygonEdges", renderParams->getRenderPolygonEdges() ? mpp::ModelRenderParams::Flag_Visible : 0);
  modelRenderParams->setMeshFlags("Border", renderParams->getRenderBorder() ? mpp::ModelRenderParams::Flag_Visible : 0);
  modelRenderParams->setMeshFlags("Triangulation", renderParams->getRenderTriangulation() ? mpp::ModelRenderParams::Flag_Visible : 0);
  modelRenderParams->setMeshFlags("Vertices", renderParams->getRenderVertices() ? mpp::ModelRenderParams::Flag_Visible : 0);
  modelRenderParams->setMeshFlags("PolygonEdgeNormals", renderParams->getRenderEdgeNormals() ? mpp::ModelRenderParams::Flag_Visible : 0);
  modelRenderParams->setMeshFlags("PolygonEdgeDirections", renderParams->getRenderEdgeDirections() ? mpp::ModelRenderParams::Flag_Visible : 0);
}

void GeometryMeshRenderer::onAddedToScene() {
  auto renderParams = static_cast<wp::viz::GeometryMeshRenderParams*>(getParams().get());

  for (auto const& kvp : mBackgroundMeshes) {
    auto const& backgroundName = kvp.first;
    renderParams->setBackgroundUniforms(backgroundName);
  }
}

void GeometryMeshRenderer::update(BoundingBox const& viewBounds, float frameTime) {
  StaticRenderer::update(viewBounds, frameTime);

  if (usingLookupGrid()) {
    auto modelRenderParams = getModelRenderParams();

    // Make all invisible
    for (auto& kvp : mBackgroundMeshes) {
      auto const& meshName = kvp.first;
      modelRenderParams->setMeshFlags(meshName, 0);
    }

    // Get visible ones and set
    auto visibleItems = mLookupGrid->getCandidateItemsInBoundingArea(viewBounds);
    for (uint32_t item : visibleItems) {
      bool include = !usingGridIncludeMasking() || gridIdInIncludeMask(item);
      bool exclude = usingGridExcludeMasking() && gridIdInExcludeMask(item);

      if (include && !exclude) {
        auto const& meshName = mBackgroundMeshLookup[item];
        modelRenderParams->setMeshFlags(meshName, mpp::ModelRenderParams::Flag_Visible);
      }
    }
  }
}

}  // namespace viz
}  // namespace WP_NAMESPACE
