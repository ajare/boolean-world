#define NOMINMAX
#define IMGUI_DEFINE_MATH_OPERATORS

#include <algorithm>
#include <exception>
#include <set>

#include <core/WorldData.h>

#include <common/GameDefines.h>

#include "imgui.h"

#include "Defines.h"
#include "Document.h"
#include "Render.h"
#include "UI.h"
#include "SelectionType.h"

using namespace std;

extern wp::Vector2 gViewOffset;
extern float gViewZoom;
extern pair<int, int> gHoveredObject;
extern pair<int, int> gSelectedObject;
extern floored::SelectionType gHoveredType;
extern floored::SelectionType gSelectedType;

// Colour maps for polygon indices
const ImU32 Colours_Deep[] = { 4289753676, 4283598045, 4285048917, 4283584196, 4289950337, 4284512403, 4291005402, 4287401100, 4285839820, 4291671396 };
const ImU32 Colours_Dark[] = { 4280031972, 4290281015, 4283084621, 4288892568, 4278222847, 4281597951, 4280833702, 4290740727, 4288256409 };
const ImU32 Colours_Pastel[] = { 4289639675, 4293119411, 4291161036, 4293184478, 4289124862, 4291624959, 4290631909, 4293712637, 4294111986 };
const ImU32 Colours_Paired[] = { 4293119554, 4290017311, 4287291314, 4281114675, 4288256763, 4280031971, 4285513725, 4278222847, 4292260554, 4288298346, 4288282623, 4280834481 };
const ImU32 Colours_Viridis[] = { 4283695428, 4285867080, 4287054913, 4287455029, 4287526954, 4287402273, 4286883874, 4285579076, 4283552122, 4280737725, 4280674301 };
const ImU32 Colours_Plasma[] = { 4287039501, 4288480321, 4289200234, 4288941455, 4287638193, 4286072780, 4284638433, 4283139314, 4281771772, 4280667900, 4280416752 };
const ImU32 Colours_Hot[] = { 4278190144, 4278190208, 4278190271, 4278190335, 4278206719, 4278223103, 4278239231, 4278255615, 4283826175, 4289396735, 4294967295 };
const ImU32 Colours_Cool[] = { 4294967040, 4294960666, 4294954035, 4294947661, 4294941030, 4294934656, 4294928025, 4294921651, 4294915020, 4294908646, 4294902015 };
const ImU32 Colours_Pink[] = { 4278190154, 4282532475, 4284308894, 4285690554, 4286879686, 4287870160, 4288794330, 4289651940, 4291685869, 4293392118, 4294967295 };
const ImU32 Colours_Jet[] = { 4289331200, 4294901760, 4294923520, 4294945280, 4294967040, 4289396565, 4283826090, 4278255615, 4278233855, 4278212095, 4278190335 };

void renderBounds(wp::BoundingBox const& bounds, wp::Vector2 const& offset, floored::Settings const& settings, ImColor colour, ImDrawList* drawList)
{
	wp::Vector2 minExtent, maxExtent;
	bounds.getExtents(minExtent, maxExtent);

	minExtent.x -= offset.x;
	minExtent.y = FE_WINDOW_HEIGHT - (minExtent.y - offset.y);

	maxExtent.x -= offset.x;
	maxExtent.y = FE_WINDOW_HEIGHT - (maxExtent.y - offset.y);

	drawList->AddRect({ minExtent.x, minExtent.y }, { maxExtent.x, maxExtent.y }, colour);
}

void renderPrimitiveGrid(float gridSize, wp::Vector2 const& offset, ImColor const& colour, float width, shared_ptr<bw::core::World> world, ImDrawList* drawList)
{
	wp::Vector2 gridOffset;
	gridOffset.x = fmod(offset.x, gridSize);
	gridOffset.y = fmod(offset.y, gridSize);

	float xMin = 0.0f, yMin = 0.0f, xMax = FE_WINDOW_WIDTH, yMax = FE_WINDOW_HEIGHT;

	if (world)
	{
		auto const& worldBounds = world->getExtents();

		wp::Vector2 minExtent, maxExtent;
		worldBounds.getExtents(minExtent, maxExtent);

		xMin = max(0.0f, minExtent.x - offset.x);
		xMax = min(maxExtent.x - offset.x, (float)FE_WINDOW_WIDTH);

		yMin = max(0.0f, minExtent.y - offset.y);
		yMax = min(maxExtent.y - offset.y, (float)FE_WINDOW_HEIGHT);
	}

	int i = 0;
	ImColor lineColour;
	
	for (float x = xMin; x <= xMax; x += gridSize, i++)
	{
		
		if (i & 1)
		{ 
			lineColour = ImColor(colour.Value.x * 0.5f, colour.Value.y * 0.5f, colour.Value.z * 0.5f, colour.Value.w);
		}
		else
		{
			lineColour = colour;
		}

		drawList->AddLine(
			{ x - gridOffset.x, FE_WINDOW_HEIGHT - yMin },
			{ x - gridOffset.x, FE_WINDOW_HEIGHT - yMax },
			lineColour,
			width
		);
	}

	i = 0;
	for (float y = yMin; y <= yMax; y += gridSize, i++)
	{
		if (i & 1)
		{
			lineColour = ImColor(colour.Value.x * 0.75f, colour.Value.y * 0.75f, colour.Value.z * 0.75f, colour.Value.w);
		}
		else
		{
			lineColour = colour;
		}

		drawList->AddLine(
			{ xMin, FE_WINDOW_HEIGHT - (y - gridOffset.y) },
			{ xMax, FE_WINDOW_HEIGHT - (y - gridOffset.y) },
			lineColour,
			width
		);
	}
}

#define TRANSFORM_V(v, offset, zoom) v -= offset; v /= zoom; v.y = FE_WINDOW_HEIGHT - v.y

void renderWorld(floored::Document* doc, floored::Settings const& settings)
{
	auto world = doc->getWorld();

	if (!world)
	{
		return;
	}

	// Triangulate, etc
	auto windowSize = wp::Vector2(FE_WINDOW_WIDTH, FE_WINDOW_HEIGHT);

	// Scale the bounds by zoom value to get the area of world we are rendering,
	// and then scale this by window size to stretch.
	auto windowSizeZoomed = windowSize * gViewZoom;
	wp::BoundingBox worldBounds(gViewOffset - windowSizeZoomed / 2, windowSizeZoomed);

	// Render
	wp::BoundingBox viewBounds(gViewOffset - windowSize / 2, windowSize);
	auto offset = viewBounds.getMinExtent();

	auto drawList = ImGui::GetBackgroundDrawList();
	auto worldOffset = worldBounds.getMinExtent();
	float renderScale = 0.5f / gViewZoom;

	auto const& polygons = doc->getPolygons();
	auto const& pslg = doc->getPSLG();
	auto const& cycles = doc->getCycles();
	auto const& hierarchy = doc->getHierarchy();
	auto const& faces = doc->getFaces();
	auto const& faceTriangles = doc->getFaceTriangles();

	// Polygons
	drawList->AddDrawCmd();
	drawList->Flags &= ~ImDrawListFlags_AntiAliasedFill;

	if (settings.renderFaces)
	{
		for (auto const& tri : faceTriangles)
		{
			auto const& v0 = pslg.vs[tri.vi[0]];
			auto const& v1 = pslg.vs[tri.vi[1]];
			auto const& v2 = pslg.vs[tri.vi[2]];

			ImVec2 iv0 = {
				(float)(v0.x) - offset.x,
				FE_WINDOW_HEIGHT - ((float)(v0.y) - offset.y)
			};

			ImVec2 iv1 = {
				(float)(v1.x) - offset.x,
				FE_WINDOW_HEIGHT - ((float)(v1.y) - offset.y)
			};

			ImVec2 iv2 = {
				(float)(v2.x) - offset.x,
				FE_WINDOW_HEIGHT - ((float)(v2.y) - offset.y)
			};

			auto const& face = faces[tri.fi];

			auto nc = sizeof(Colours_Deep) / sizeof(Colours_Deep[0]);
			ImColor c = Colours_Deep[face.owningPolygon % nc];
			
			if (gSelectedType == floored::SelectionType::Face && gSelectedObject.first == tri.fi)
			{
				c = ImColor(1.0f, 1.0f, 0.0f);
			}
			else if (gHoveredType == floored::SelectionType::Face && gHoveredObject.first == tri.fi)
			{
				c = ImColor(1.0f, 0.5f, 0.0f);
			}
			
			c.Value.w = 0.5f;

			drawList->AddTriangleFilled(iv0, iv1, iv2, c);
		}
	}

	// Polygons
	switch (settings.edgeRenderMode)
	{
	case floored::Settings::EdgeRenderMode::Polygons:
		for (int i = 0; i < (int)polygons.size(); ++i)
		{
			auto const& polygon = polygons[i];

			auto numVertices = (int)polygon.path.size();

			for (int j = 0; j < numVertices; ++j)
			{
				int k = (j + 1) % numVertices;

				ImVec2 v0 = {
					(float)(polygon.path[j].x / 1000.0) - offset.x,
					FE_WINDOW_HEIGHT - ((float)(polygon.path[j].y / 1000.0) - offset.y)
				};

				ImVec2 v1 = {
					(float)(polygon.path[k].x / 1000.0) - offset.x,
					FE_WINDOW_HEIGHT - ((float)(polygon.path[k].y / 1000.0) - offset.y)
				};

				auto nc = sizeof(Colours_Deep) / sizeof(Colours_Deep[0]);
				ImColor c = Colours_Deep[i % nc];

				if (gSelectedType == floored::SelectionType::PolygonEdge && gSelectedObject.first == i && gSelectedObject.second == j)
				{
					c = ImColor(1.0f, 1.0f, 0.0f);
				}
				else if (gHoveredType == floored::SelectionType::PolygonEdge && gHoveredObject.first == i && gHoveredObject.second == j)
				{
					c = ImColor(1.0f, 0.5f, 0.0f);
				}

				if (polygon.isHole)
				{
					c.Value.w = 0.5f;
				}

				drawList->AddLine(v0, v1, c, 2.0f);
			}
		}
		break;

	case floored::Settings::EdgeRenderMode::Graph:
		for (int i = 0; i < (int)pslg.es.size(); ++i)
		{
			auto const& edge = pslg.es[i];

			auto const& v0 = pslg.vs[edge.vi[0]];
			auto const& v1 = pslg.vs[edge.vi[1]];

			ImVec2 iv0 = {
				(float)(v0.x) - offset.x,
				FE_WINDOW_HEIGHT - ((float)(v0.y) - offset.y)
			};

			ImVec2 iv1 = {
				(float)(v1.x) - offset.x,
				FE_WINDOW_HEIGHT - ((float)(v1.y) - offset.y)
			};

			ImColor c;
			float width;

			if (!edge.doubleSided())
			{
				auto nc = sizeof(Colours_Deep) / sizeof(Colours_Deep[0]);
				auto cc = Colours_Deep[edge.fi[0] % nc];
				c = cc;
				width = 3.0f;
			}
			else
			{
				c = ImColor(0.5f, 0.5f, 0.5f, 0.75f);
				width = 1.5f;
			}

			if (gSelectedType == floored::SelectionType::GraphEdge && gSelectedObject.first == i)
			{
				c = ImColor(1.0f, 1.0f, 0.0f);
			}
			else if (gHoveredType == floored::SelectionType::GraphEdge && gHoveredObject.first == i)
			{
				c = ImColor(1.0f, 0.5f, 0.0f);
			}

			drawList->AddLine(iv0, iv1, c, width);
		}

		if (settings.renderGraphVertices)
		{
			for (auto const& v : pslg.vs)
			{
				ImVec2 iv = {
					(float)(v.x) - offset.x,
					FE_WINDOW_HEIGHT - ((float)(v.y) - offset.y)
				};

				drawList->AddRectFilled({ iv.x - 2, iv.y - 2 }, { iv.x + 2, iv.y + 2 }, settings.graphVertexColour, 0.3f);
			}
		}
		break;

	case floored::Settings::EdgeRenderMode::None:
		break;

	default:
		break;
	}

	// Primitives
	auto primitives = world->getPrimitives();
			
	for (auto primitive : primitives)
	{
		// Transformed vertices
		if (settings.renderPrimitives)
		{
			auto complexPolygons = primitive->getVertices();

			for (auto const& complexPolygon : complexPolygons)
			{
				for (auto const& polygon : complexPolygon)
				{
					auto numVertices = (int)polygon.size();
					vector<ImVec2> imPoints(numVertices);

					for (int i = 0; i < numVertices; ++i)
					{
						imPoints[i] = {
							polygon[i].p.x - offset.x,
							FE_WINDOW_HEIGHT - (polygon[i].p.y - offset.y)
						};
					}

					drawList->AddPolyline(imPoints.data(), numVertices, settings.primitiveColour, ImDrawFlags_Closed, 1.5f);
				}
			}
		}

		// Position and size
		auto primPos = primitive->getPosition();
		TRANSFORM_V(primPos, worldOffset, gViewZoom);

		// Debug info
		if (settings.renderPrimitiveDebug)
		{
			auto debugText = format("id: {}", primitive->getId());
			drawList->AddText({ primPos.x + 4, primPos.y }, ImColor(0.8f, 0.8f, 0.0f), debugText.c_str());
		}

		// Bounds
		if (settings.renderPrimitiveBounds)
		{
			renderBounds(primitive->getBounds(), offset, settings, settings.primitiveBoundsColour, drawList);
		}
	}
}