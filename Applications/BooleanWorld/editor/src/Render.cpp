#define NOMINMAX

#include <algorithm>
#include <iostream>

#pragma warning(push)
#pragma warning(disable : 4307)
#include <spdlog/spdlog.h>
#pragma warning(pop)

#include <core/WorldData.h>
#include <core/ClipperDefines.h>
#include <core/Utils.h>
#include <core/SquareTiling.h>

#include <common/GameDefines.h>

#include "imgui.h"

#include "Defines.h"
#include "Document.h"
#include "Render.h"
#include "UiHelpers.h"

using namespace std;

extern spdlog::logger* gLogger;
extern wp::Vector2 gViewOffset;
extern int gHoveredPrimitiveHandle;

// const ImU32 gColours[] = { 4289639675, 4293119411, 4291161036, 4293184478, 4289124862, 4291624959, 4290631909, 4293712637, 4294111986 };
const ImU32 gColours[] = {4283695428, 4285867080, 4287054913, 4287455029, 4287526954, 4287402273, 4286883874, 4285579076, 4283552122, 4280737725, 4280674301};

void renderBounds(wp::BoundingBox const& bounds, wp::Vector2 const& offset, editor::Settings const& settings, ImColor colour, ImDrawList* drawList) {
  wp::Vector2 minExtent, maxExtent;
  bounds.getExtents(minExtent, maxExtent);

  minExtent.x -= offset.x;
  minExtent.y = ED_WINDOW_HEIGHT - (minExtent.y - offset.y);

  maxExtent.x -= offset.x;
  maxExtent.y = ED_WINDOW_HEIGHT - (maxExtent.y - offset.y);

  drawList->AddRect({minExtent.x, minExtent.y}, {maxExtent.x, maxExtent.y}, colour);
}

void renderGrid(float gridSize, wp::Vector2 const& offset, ImColor const& colour, float width, shared_ptr<bw::core::World> world, ImDrawList* drawList) {
  wp::Vector2 gridOffset;
  gridOffset.x = fmod(offset.x, gridSize);
  gridOffset.y = fmod(offset.y, gridSize);

  float xMin = 0.0f, yMin = 0.0f, xMax = ED_WINDOW_WIDTH, yMax = ED_WINDOW_HEIGHT;

  if (world) {
    auto const& worldBounds = world->getExtents();

    wp::Vector2 minExtent, maxExtent;
    worldBounds.getExtents(minExtent, maxExtent);

    xMin = max(0.0f, minExtent.x - offset.x);
    xMax = min(maxExtent.x - offset.x, (float)ED_WINDOW_WIDTH);

    yMin = max(0.0f, minExtent.y - offset.y);
    yMax = min(maxExtent.y - offset.y, (float)ED_WINDOW_HEIGHT);
  }

  for (float x = xMin; x <= xMax; x += gridSize) {
    drawList->AddLine(
        {x - gridOffset.x, ED_WINDOW_HEIGHT - yMin},
        {x - gridOffset.x, ED_WINDOW_HEIGHT - yMax},
        colour,
        width);
  }

  for (float y = yMin; y <= yMax; y += gridSize) {
    drawList->AddLine(
        {xMin, ED_WINDOW_HEIGHT - (y - gridOffset.y)},
        {xMax, ED_WINDOW_HEIGHT - (y - gridOffset.y)},
        colour,
        width);
  }
}

void renderPrefabTiles(bw::core::PrefabAreaTilingType type, wp::Vector2 const& offset, ImDrawList* drawList) {
  vector<wp::Vector2> verts;

  switch (type) {
    case bw::core::PrefabAreaTilingType::None:
      return;

    case bw::core::PrefabAreaTilingType::Square:
      verts = bw::core::SquareTiling(BW_PLAYER_RADIUS * BW_PREFAB_PLAYER_RATIO).generateTileOutline({0.0f, 0.0}, 0.0f, 0);
      break;
  }

  auto numVerts = (uint32_t)verts.size();
  vector<ImVec2> imPoints(numVerts);

  for (uint32_t i = 0; i < numVerts; ++i) {
    imPoints[i] = {
        verts[i].x - offset.x,
        ED_WINDOW_HEIGHT - (verts[i].y - offset.y)};
  }

  drawList->AddPolyline(imPoints.data(), numVerts, ImColor(0.5f, 0.8f, 1.0f), ImDrawFlags_Closed, 1.0f);
}

void renderWorld(
    editor::Document* doc,
    editor::Settings const& settings,
    bw::core::ArrangementWorldData const* worldData,
    double globalTime) {
  auto world = doc->getWorld();

  bool renderWorldStuff = world != nullptr;

  // Triangulate, etc
  auto windowSize = wp::Vector2(ED_WINDOW_WIDTH, ED_WINDOW_HEIGHT);
  wp::BoundingBox viewBounds(gViewOffset - windowSize / 2, windowSize);

  // Get all primitives for now
  vector<bw::core::Primitive*> primitives;
  vector<bw::core::WorldTriggerLine*> triggerLines;

  if (world) {
    primitives = world->findPrimitives(viewBounds);
    triggerLines = world->findTriggerLines(viewBounds);
  }

  auto numPrimitives = (uint32_t)primitives.size();
  bool renderPrimitiveStuff = numPrimitives > 0;

  // Render
  auto drawList = ImGui::GetBackgroundDrawList();
  auto offset = viewBounds.getMinExtent();

  if (renderPrimitiveStuff) {
    auto const& arrangement = worldData->getArrangement();
    auto const& triangles = worldData->getTriangles();
    auto toWorld = [&](uint32_t vertexIndex) {
      auto const& vertex = arrangement.vertices[vertexIndex];
      return wp::Vector2{
          float(vertex.x / BW_CLIPPER_SCALE),
          float(vertex.y / BW_CLIPPER_SCALE)};
    };

    auto const& selectedPrimitiveIndices = doc->getSelectedPrimitiveIndices();

    if (!triangles.empty()) {
      drawList->AddDrawCmd();
      drawList->Flags &= ~ImDrawListFlags_AntiAliasedFill;

      for (auto const& triangle : triangles) {
        auto v0 = toWorld(triangle.v[0]);
        auto v1 = toWorld(triangle.v[1]);
        auto v2 = toWorld(triangle.v[2]);
        if (settings.renderTriangulation) {
          drawList->AddTriangle(
              {v0.x - offset.x, ED_WINDOW_HEIGHT - (v0.y - offset.y)},
              {v1.x - offset.x, ED_WINDOW_HEIGHT - (v1.y - offset.y)},
              {v2.x - offset.x, ED_WINDOW_HEIGHT - (v2.y - offset.y)},
              settings.triangulationColour);
        } else {
          drawList->AddTriangleFilled(
              {v0.x - offset.x, ED_WINDOW_HEIGHT - (v0.y - offset.y)},
              {v1.x - offset.x, ED_WINDOW_HEIGHT - (v1.y - offset.y)},
              {v2.x - offset.x, ED_WINDOW_HEIGHT - (v2.y - offset.y)},
              settings.backgroundColour);
        }
      }

      if (settings.renderWorldBorder) {
        drawList->AddDrawCmd();
        for (auto const& wall : worldData->getWalls()) {
          if (wall.kind != expr::ArrangementWallKind::Border) {
            continue;
          }
          auto const& edge = arrangement.edges[wall.edge];
          auto v0 = toWorld(edge.v[0]);
          auto v1 = toWorld(edge.v[1]);
          drawList->AddLine(
              {v0.x - offset.x, ED_WINDOW_HEIGHT - (v0.y - offset.y)},
              {v1.x - offset.x, ED_WINDOW_HEIGHT - (v1.y - offset.y)},
              settings.borderColour,
              3.0f);
        }
      }
    }

    // Primitives
    if (!primitives.empty()) {
      drawList->AddDrawCmd();

      for (auto primitive : primitives) {
        bool isGhost = (primitive->getFlags() & BW_PRIMITIVE_GHOST_FLAG) != 0;
        if (isGhost && !settings.ghostActive) {
          continue;
        }

        if (!primitive->isStatic() && !settings.renderAnimatedPrimitives) {
          continue;
        }

        auto primitiveId = primitive->getId();
        bool selected = selectedPrimitiveIndices.find(primitiveId) != selectedPrimitiveIndices.end();

        // Primitive borders
        vector<ImVec2> ghostBorderPoints;

        if (settings.renderPrimitiveBorders || isGhost || selected) {
          auto complexPolygons = primitive->getVertices();

          for (auto const& complexPolygon : complexPolygons) {
            for (auto const& polygon : complexPolygon) {
              auto numVertices = (int)polygon.size();
              vector<ImVec2> imPoints(numVertices);

              for (int i = 0; i < numVertices; ++i) {
                imPoints[i] = {
                    polygon[i].p.x - offset.x,
                    ED_WINDOW_HEIGHT - (polygon[i].p.y - offset.y)};
              }

              if (isGhost) {
                // Defer ghost to end, so that its lines are always visible
                ghostBorderPoints = imPoints;
              } else if (selected) {
                drawList->AddPolyline(imPoints.data(), numVertices, settings.selectedPrimitiveColour, ImDrawFlags_Closed, 2.5f);
              } else {
                drawList->AddPolyline(imPoints.data(), numVertices, settings.primitiveColour, ImDrawFlags_Closed, 1.5f);
              }
            }
          }
        }

        if (!ghostBorderPoints.empty()) {
          drawList->AddPolyline(ghostBorderPoints.data(), (int)ghostBorderPoints.size(), settings.ghostPrimitiveColour, ImDrawFlags_Closed, 2.0f);
        }
      }

      // Influence origin
      if (settings.renderInfluenceEyes) {
        drawList->AddDrawCmd();
        drawList->Flags |= ImDrawListFlags_AntiAliasedFill;

        for (auto primitive : primitives) {
          if (!primitive->isStatic() && !settings.renderAnimatedPrimitives) {
            continue;
          }

          bool selected = selectedPrimitiveIndices.find(primitive->getId()) != selectedPrimitiveIndices.end();
          bool ghost = primitive->getFlags() & BW_PRIMITIVE_GHOST_FLAG;

          if (!selected || ghost) {
            continue;
          }

          // Influence centre
          auto influenceCentre = primitive->getInfluenceEyeOriginPosition();

          influenceCentre.x -= offset.x;
          influenceCentre.y = ED_WINDOW_HEIGHT - (influenceCentre.y - offset.y);

          drawList->AddCircleFilled({influenceCentre.x, influenceCentre.y}, 6, settings.influenceEyeColour);

          drawList->AddBezierQuadratic(
              {influenceCentre.x - 17, influenceCentre.y},
              {influenceCentre.x, influenceCentre.y + 11},
              {influenceCentre.x + 17, influenceCentre.y},
              settings.influenceEyeColour,
              1.5f);

          drawList->AddBezierQuadratic(
              {influenceCentre.x + 17, influenceCentre.y},
              {influenceCentre.x, influenceCentre.y - 11},
              {influenceCentre.x - 17, influenceCentre.y},
              settings.influenceEyeColour,
              1.5f);

          // Angle offset
          auto eyeAngleOffset = primitive->getInfluenceEyeAngleOffset();
          auto eyeAngleOffsetDir = wp::Vector2(0, -1).rotatedAnticlockwiseCopy(eyeAngleOffset);
          auto eyeAngleOffsetPos = influenceCentre + eyeAngleOffsetDir * 30;

          drawList->AddLine(
              {influenceCentre.x, influenceCentre.y},
              {eyeAngleOffsetPos.x, eyeAngleOffsetPos.y},
              settings.influenceEyeColour,
              1.5f);
        }
      }
    }

    //
    // Clipped vertices
    //
    if (settings.renderClippedVertices) {
      for (auto const& vertex : arrangement.vertices) {
        wp::Vector2 p{
            float(vertex.x / BW_CLIPPER_SCALE),
            float(vertex.y / BW_CLIPPER_SCALE)};
        p.x -= offset.x;
        p.y = ED_WINDOW_HEIGHT - (p.y - offset.y);
        drawList->AddCircle(
            {p.x, p.y}, settings.vertexRadius, settings.vertexColour, 16, 1.0f);
      }
    }

    //
    // Player start
    //
    auto playerStartPos = world->getPlayerStartPosition();
    auto playerStartAngle = world->getPlayerStartAngle();
    auto startLookPos = playerStartPos + wp::Vector2(0, 1).rotatedCopy(playerStartAngle) * (BW_PLAYER_RADIUS + 20);

    playerStartPos.x -= offset.x;
    playerStartPos.y = ED_WINDOW_HEIGHT - (playerStartPos.y - offset.y);

    drawList->AddCircle({playerStartPos.x, playerStartPos.y}, BW_PLAYER_RADIUS + 20, ImColor(0, 1, 0), BW_PLAYER_RADIUS * 2, 2);

    startLookPos.x -= offset.x;
    startLookPos.y = ED_WINDOW_HEIGHT - (startLookPos.y - offset.y);

    drawList->AddLine({playerStartPos.x, playerStartPos.y}, {startLookPos.x, startLookPos.y}, ImColor(0, 1, 0), 2);

    //
    // Player proxy
    //
    drawList->AddDrawCmd();

    auto playerProxyPos = doc->getPlayerProxyPosition();
    float playerProxyAngle = doc->getPlayerProxyAngle();

    playerProxyPos.x -= offset.x;
    playerProxyPos.y = ED_WINDOW_HEIGHT - (playerProxyPos.y - offset.y);

    if (settings.renderPlayerView) {
      drawList->AddCircleFilled({playerProxyPos.x, playerProxyPos.y}, BW_PLAYER_VIEW_DISTANCE, ImColor(1.0f, 1.0f, 1.0f, 0.3f));
    }

    drawList->AddCircleFilled({playerProxyPos.x, playerProxyPos.y}, BW_PLAYER_RADIUS, settings.playerProxyColour);

    // FOV - flip Y values
    if (settings.renderPlayerView) {
      auto v0 = playerProxyPos;
      auto [v1, v2] = bw::core::calculateFovTriangle(v0, playerProxyAngle - 180, BW_PLAYER_VIEW_DISTANCE, BW_PLAYER_FOV);

      drawList->AddTriangleFilled({v0.x, v0.y}, {v1.x, v1.y}, {v2.x, v2.y}, ImColor(0.0f, 0.5f, 0.7f, 0.4f));
    }

    // Influence circles
    if (settings.renderInfluenceEyes) {
      if (!primitives.empty()) {
        drawList->AddDrawCmd();

        for (auto const* primitive : primitives) {
          if (!primitive->isStatic() && !settings.renderAnimatedPrimitives) {
            continue;
          }

          bool selected = selectedPrimitiveIndices.find(primitive->getId()) != selectedPrimitiveIndices.end();
          bool ghost = primitive->getFlags() & BW_PRIMITIVE_GHOST_FLAG;

          if (!selected || ghost) {
            continue;
          }

          // Time update distance
          if (settings.renderTimeUpdateDistance) {
            auto primPosition = primitive->getPosition();
            auto tuDist = primitive->getTimeUpdateDistance();

            if (tuDist < 1000.0f) {
              primPosition.x -= offset.x;
              primPosition.y = ED_WINDOW_HEIGHT - (primPosition.y - offset.y);

              int numSegs = (int)(tuDist / 8);
              drawList->AddCircle({primPosition.x, primPosition.y}, tuDist, settings.timeUpdateDistColour, numSegs, 1.5f);
            }
          }

          auto influenceCentre = primitive->getInfluenceEyeOriginPosition();

          influenceCentre.x -= offset.x;
          influenceCentre.y = ED_WINDOW_HEIGHT - (influenceCentre.y - offset.y);

          for (int i = 0; i < (int)bw::core::VertexTransformer::Key::COUNT; ++i) {
            if (i == 0 && !settings.renderScaleInfluenceZones ||
                i == 1 && !settings.renderAngleInfluenceZones ||
                i == 2 && !settings.renderOrbitAngleInfluenceZones ||
                i == 3 && !settings.renderOrbitDistanceInfluenceZones) {
              continue;
            }

            auto const& lerper = primitive->getInfluenceInterpolator((bw::core::VertexTransformer::Key)i);
            auto const& point = lerper.getPoints();

            wp::Vector2 scaleMin, scaleMax;
            lerper.getScale(&scaleMin, &scaleMax);

            auto colour = settings.influenceColours[i];

            for (auto const& point : point) {
              int numSegs = (int)point.first;
              float radius = scaleMax.x - point.first;
              drawList->AddCircle({influenceCentre.x, influenceCentre.y}, radius, colour, numSegs, 1.5f);
            }
          }
        }
      }
    }

    // Bounding boxes
    if (settings.renderPrimitiveBounds && !primitives.empty()) {
      drawList->AddDrawCmd();

      for (auto primitive : primitives) {
        if (primitive->getId() == ED_GHOST_INDEX && !settings.ghostActive) {
          continue;
        }

        if (!primitive->isStatic() && !settings.renderAnimatedPrimitives) {
          continue;
        }

        renderBounds(primitive->getBounds(), offset, settings, settings.animatedBoundsColour, drawList);
      }
    }
  }

  // Triggers
  if (settings.renderTriggerLines) {
    for (auto triggerLine : triggerLines) {
      wp::Vector2 points[2];

      points[0] = triggerLine->getPoint(0);
      points[1] = triggerLine->getPoint(1);

      wp::Vector2 p0 = {points[0].x - offset.x, ED_WINDOW_HEIGHT - (points[0].y - offset.y)};
      wp::Vector2 p1 = {points[1].x - offset.x, ED_WINDOW_HEIGHT - (points[1].y - offset.y)};

      auto colour = settings.triggerLineColour;
      if (triggerLine->getId() == doc->getSelectedTriggerLineIndex()) {
        colour = ImColor(1.0f, 0.5f, 0.5f);
      }

      drawList->AddLine({p0.x, p0.y}, {p1.x, p1.y}, colour, settings.triggerLineHandleRadius - 2);

      // Handles
      drawList->AddCircleFilled({p0.x, p0.y}, settings.triggerLineHandleRadius, colour, 16);
      drawList->AddCircleFilled({p1.x, p1.y}, settings.triggerLineHandleRadius, colour, 16);

      // Side markers
      auto centre = p0.lerp(p1, 0.5f);
      auto dir = p0.directionTo(p1);
      auto side = dir * 10;
      auto v1 = centre + side;
      auto v2 = centre - side;
      auto normal = dir.perpendicular() * 12;
      auto v3 = centre + normal;
      auto v4 = centre - normal;

      drawList->AddTriangleFilled({v1.x, v1.y}, {v2.x, v2.y}, {v3.x, v3.y}, settings.triggerLineBlue);
      drawList->AddTriangleFilled({v1.x, v1.y}, {v2.x, v2.y}, {v4.x, v4.y}, settings.triggerLineRed);
    }
  }

  // Grid
  if (settings.showGrid) {
    renderGrid(settings.gridSize, offset, settings.gridColour, 1.0f, renderWorldStuff ? world : nullptr, drawList);
  }

  // Prefab tiles
  if (settings.renderPrefabTiles) {
    renderPrefabTiles(world->getPrefabAreaTilingType(), offset, drawList);
  }
}

wp::Vector2 worldToMinimap(wp::Vector2 const& worldPos, wp::BoundingBox const& worldBounds, wp::BoundingBox const& mapBounds) {
  auto dp = (worldPos - worldBounds.getMinExtent()) / MINIMAP_SCALE;
  auto mapMin = mapBounds.getMinExtent();
  auto mapMax = mapBounds.getMaxExtent();

  dp.x += mapMin.x;
  dp.y = mapMax.y - dp.y;

  return dp;
}

wp::BoundingBox getMiniMapBounds(editor::Document* doc) {
  auto windowSize = wp::Vector2(ED_WINDOW_WIDTH, ED_WINDOW_HEIGHT);

  auto world = doc->getWorld();
  auto worldBounds = world->getExtents();
  auto worldSize = worldBounds.getSize() / MINIMAP_SCALE;

  float x0 = windowSize.x - worldSize.x;
  float y0 = windowSize.y - MINIMAP_Y_OFFSET;
  float x1 = x0 + worldSize.x;
  float y1 = y0 - worldSize.y;

  return wp::BoundingBox(x0, y0, x1 - x0, y1 - y0);
}

void renderMiniMap(editor::Document* doc, editor::Settings const& settings, bw::core::WorldData const* worldData, double globalTime) {
  auto windowSize = wp::Vector2(ED_WINDOW_WIDTH, ED_WINDOW_HEIGHT);

  // Render
  auto drawList = ImGui::GetBackgroundDrawList();

  auto world = doc->getWorld();
  auto worldBounds = world->getExtents();

  auto mmBounds = getMiniMapBounds(doc);
  wp::Vector2 mmMin, mmMax;

  mmBounds.getExtents(mmMin, mmMax);

  // Primitives
  auto primitives = world->getPrimitives();

  for (auto primitive : primitives) {
    auto pc = primitive->getPosition();
    auto ps = primitive->getSize();

    auto pb0 = worldToMinimap(pc - ps * 0.5f, worldBounds, mmBounds);
    auto pb1 = worldToMinimap(pc + ps * 0.5f, worldBounds, mmBounds);

    ImColor bColour = primitive->isStatic() ? ImColor(1.0f, 0.0f, 1.0f, 0.5f) : ImColor(1.0f, 1.0f, 0.0f, 0.5f);
    ImColor fColour = ImColor(0.2f, 0.2f, 1.0f, 0.5f);

    drawList->AddRectFilled({pb0.x, pb0.y}, {pb1.x, pb1.y}, fColour);
    drawList->AddRect({pb0.x, pb0.y}, {pb1.x, pb1.y}, bColour);
  }

  // View window
  wp::BoundingBox viewBounds(gViewOffset - windowSize / 2, windowSize);

  auto b0 = worldToMinimap(viewBounds.getMinExtent(), worldBounds, mmBounds);
  auto b1 = worldToMinimap(viewBounds.getMaxExtent(), worldBounds, mmBounds);

  drawList->AddRect({b0.x, b0.y}, {b1.x, b1.y}, ImColor(1.0f, 0.5f, 0.0f));

  // Border
  drawList->AddRect({mmMin.x, mmMin.y}, {mmMax.x, mmMax.y}, ImColor(1.0f, 0.0f, 0.0f));
}