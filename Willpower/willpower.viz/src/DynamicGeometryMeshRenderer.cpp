#include <algorithm>

#include <mpp/ModelInstance.h>
#include <mpp/TriangleBatch.h>

#include "willpower/geometry/Mesh.h"
#include "willpower/geometry/MeshUtils.h"

#include "willpower/viz/DynamicGeometryMeshRenderer.h"
#include "willpower/viz/DynamicGeometryMeshRenderParams.h"

#undef min
#undef max

namespace WP_NAMESPACE
{
	namespace viz
	{

		using namespace std;
		using namespace wp;

		DynamicGeometryMeshRenderer::DynamicGeometryMeshRenderer(string const& name, vector<Renderable const*> objects, RenderParams* params, Vector2 const& minExtent, Vector2 const& maxExtent)
			: DynamicRenderer(name, objects, params, minExtent, maxExtent)
		{
		}

		mpp::Batch* DynamicGeometryMeshRenderer::instantiateLineBatch(string const& name, int size, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
		{
			auto batch = new mpp::LineBatch(
				name,
				{
					mpp::mesh::Vertex::DataType::Float,
					mpp::mesh::Vertex::DataType::UnsignedByte,
					true
				},
				size,
				renderSystem,
				resourceMgr);

			batch->load();
			return batch;
		}

		mpp::Batch* DynamicGeometryMeshRenderer::instantiatePointBatch(string const& name, int size, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
		{
			/*
			auto batch = new mpp::QuadBatch(
				name,
				mpp::QuadBatch::VertexOptions::Auto,
				mpp::mesh::Vertex::DataType::Float,
				mpp::mesh::Vertex::DataType::Float,
				mpp::mesh::Vertex::DataType::UnsignedByte,
				false,
				false,
				16,
				16,
				"",
				false,
				32,
				size,
				renderSystem,
				resourceMgr);

			batch->load();
			return batch;
			*/
			return nullptr;
		}

		mpp::Batch* DynamicGeometryMeshRenderer::instantiateTriangleBatch(string const& name, int size, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
		{
			/*
			auto batch = new mpp::TriangleBatch(
				name,
				mpp::mesh::Vertex::DataType::Float,
				mpp::mesh::Vertex::DataType::Float,
				mpp::mesh::Vertex::DataType::UnsignedByte,
				size,
				"",
				renderSystem,
				resourceMgr
			);

			batch->load();
			return batch;
			*/
			return nullptr;
		}

		void DynamicGeometryMeshRenderer::updateBatch(Renderable const* object, mpp::Batch* batch, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
		{
			WP_UNUSED(resourceMgr);
			WP_UNUSED(renderSystem);
			/*
			geometry::Mesh const* mesh = static_cast<geometry::Mesh const*>(object);
			DynamicGeometryMeshRenderParams const* params = static_cast<DynamicGeometryMeshRenderParams const*>(mParams);

			string name = batch->getName();
			if (name == getName() + "_vertices")
			{
				int numVertices = mesh->getNumVertices();
				auto const& colour = params->getVertexColour();

				batch->startUpdate(numVertices);
				float* posDataDst = (float*)batch->getPositionData();

				uint32_t vertexIndex = mesh->getFirstVertexIndex();
				while (!mesh->vertexIndexIterationFinished(vertexIndex))
				{
					auto const& vertex = mesh->getVertex(vertexIndex);
					auto const& vertexPos = vertex.getPosition();

					*posDataDst++ = vertexPos.x;
					*posDataDst++ = vertexPos.y;
					*posDataDst++ = colour.red;
					*posDataDst++ = colour.green;
					*posDataDst++ = colour.blue;
					*posDataDst++ = colour.alpha;
					
					vertexIndex = mesh->getNextVertexIndex(vertexIndex);
				}

				batch->finishUpdate(numVertices, false);
			}
			else if (name == getName() + "_borders")
			{
				float borderWidth = params->getBorderWidth();
				mpp::Colour const& colour = params->getBorderColour();

				// Triangles
				vector<float> borderTriangles;
				list<geometry::MeshUtils::EdgeIndexInfo> borderEdgeData;

				uint32_t edgeIndex = mesh->getFirstEdgeIndex();
				while (!mesh->edgeIndexIterationFinished(edgeIndex))
				{
					auto const& edge = mesh->getEdge(edgeIndex);

					switch (edge.getConnectivity())
					{
					case geometry::Edge::Connectivity::External:
						borderEdgeData.push_back(make_tuple(
							edgeIndex,
							edge.getFirstVertex(),
							edge.getSecondVertex()));
						break;
					default:
						break;
					}

					edgeIndex = mesh->getNextEdgeIndex(edgeIndex);
				}

				vector<vector<geometry::MeshUtils::EdgeIndexInfo>> orderedBorderEdges =
					geometry::MeshUtils::groupConnectedEdges(borderEdgeData);

				int numBorderTris = 0;
				vector<Vector2> vertexPositions;
				for (auto const& v: orderedBorderEdges)
				{
					vector<Vector2> borderVerts;
					for (auto const& borderEdge: v)
					{
						auto const& edge = mesh->getEdge(get<0>(borderEdge));
						auto const& v0 = mesh->getVertex(edge.getFirstVertex());
						borderVerts.push_back(v0.getPosition());
					}

					float widthMod = MathsUtils::pointsWinding(borderVerts) == Winding::Anticlockwise ? -1.0f : 1.0f;
					auto newVerts = geometry::MeshUtils::insetVertexLoop(borderVerts, borderWidth * widthMod, params->getBordersRounded());

					// Triangulate space between borderVerts and offsetVerts
					TriangleData borderTris;
					borderTris.build(newVerts[0], { borderVerts });

					numBorderTris += borderTris.getNumTriangles();
					for (int i = 0; i < borderTris.getNumTriangles(); ++i)
					{
						Vector2 tv0, tv1, tv2;
						borderTris.getTriangle(i, tv0, tv1, tv2);

						vertexPositions.push_back(tv0);
						vertexPositions.push_back(tv1);
						vertexPositions.push_back(tv2);
					}
				}

				// Update batch
				batch->startUpdate(numBorderTris);
				float* posDataDst = (float*)batch->getPositionData();

				for (auto const& vertexPos: vertexPositions)
				{
					*posDataDst++ = vertexPos.x;
					*posDataDst++ = vertexPos.y;
					*posDataDst++ = 0.0f;
					*posDataDst++ = 0.0f;
					*posDataDst++ = colour.red;
					*posDataDst++ = colour.green;
					*posDataDst++ = colour.blue;
					*posDataDst++ = colour.alpha;
				}

				batch->finishUpdate(numBorderTris, false);
			}
			*/
		}

		void DynamicGeometryMeshRenderer::createBatch(Renderable const* object, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
		{
			/*
			geometry::Mesh const* mesh = static_cast<geometry::Mesh const*>(object);
			DynamicGeometryMeshRenderParams const* params = static_cast<DynamicGeometryMeshRenderParams const*>(mParams);

			// Lines: interior edges (polygon borders) and borders
			int numEdges = 0, numBorders = 0;

			if (params->getRenderBorders() || params->getRenderPolygonBorders())
			{
				uint32_t edgeIndex = mesh->getFirstEdgeIndex();
				while (!mesh->edgeIndexIterationFinished(edgeIndex))
				{
					auto const& edge = mesh->getEdge(edgeIndex);

					switch (edge.getConnectivity())
					{
					case geometry::Edge::Connectivity::External:
						numBorders++;
						break;
					case geometry::Edge::Connectivity::Internal:
						numEdges++;
						break;
					default:
						break;
					}

					edgeIndex = mesh->getNextEdgeIndex(edgeIndex);
				}
			}

			if (params->getRenderBorders())
			{
				float borderWidth = params->getBorderWidth();

				if (borderWidth <= 0)
				{
					// Lines
				}
				else
				{
					auto borderBatch = instantiateTriangleBatch(getName() + "_borders", 256, renderSystem, resourceMgr);
					updateBatch(object, borderBatch, renderSystem, resourceMgr);

					// Add
					auto it = mBatches.insert(make_pair(object, vector<BatchInstance>()));

					BatchInstance bi;
					bi.batch = borderBatch;
					bi.fn = [](mpp::MeshInstance* mi, RenderParams const* params) 
					{
						WP_UNUSED(mi);
						WP_UNUSED(params);
					};

					it.first->second.push_back(bi);

				}
			}

			// Vertices
			if (params->getRenderVertices())
			{
				int numVertices = mesh->getNumVertices();
				auto vertexBatch = instantiatePointBatch(getName() + "_vertices", numVertices, renderSystem, resourceMgr);
				updateBatch(object, vertexBatch, renderSystem, resourceMgr);

				// Add
				auto it = mBatches.insert(make_pair(object, vector<BatchInstance>()));

				BatchInstance bi;
				bi.batch = vertexBatch;
				bi.fn = [](mpp::MeshInstance* mi, RenderParams const* params) 
				{
					WP_UNUSED(mi);
					WP_UNUSED(params);
				};

				it.first->second.push_back(bi);
			}
			*/
		}

	} // viz
} // WP_NAMESPACE
