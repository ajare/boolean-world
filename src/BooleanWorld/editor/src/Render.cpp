#define NOMINMAX

#include <algorithm>
#include <cmath>
#include <iostream>

#pragma warning(push)
#pragma warning(disable : 4307)
#include <spdlog/spdlog.h>
#pragma warning(pop)

#include <core/WorldData.h>
#include <core/Utils.h>
#include <core/Layer.h>
#include <core/PrimitiveField.h>

#include <common/GameDefines.h>

#include "imgui.h"

#include "Defines.h"
#include "Document.h"
#include "PrimitiveFieldPreview.h"
#include "Render.h"
#include "UiHelpers.h"

using namespace std;

extern spdlog::logger* gLogger;
extern wp::Vector2 gViewOffset;
extern float gViewZoom;
extern int gHoveredPrimitiveHandle;
extern wp::Vector2 gWorldViewScreenOrigin;
extern wp::Vector2 gWorldViewSize;

// Converts a point in canvas-local pixel space (0,0 at the World window's
// top-left, y-down) to the absolute screen space that ImGui draw lists use.
static ImVec2 toScreen(wp::Vector2 const& canvasPoint) {
  return {
      gWorldViewScreenOrigin.x + canvasPoint.x,
      gWorldViewScreenOrigin.y + canvasPoint.y};
}

static ImVec2 toScreen(float canvasX, float canvasY) {
  return {gWorldViewScreenOrigin.x + canvasX, gWorldViewScreenOrigin.y + canvasY};
}

// Converts a point in world space to absolute screen space, applying pan
// (gViewOffset), zoom (gViewZoom) and the world-to-canvas y-flip in one step.
// This is the single source of truth for where a world point lands on
// screen; every other world-space transform in this file should go through
// it rather than re-deriving the pan/zoom/flip maths inline.
static ImVec2 worldToScreen(wp::Vector2 const& worldPoint) {
  return toScreen(
      (worldPoint.x - gViewOffset.x) * gViewZoom + gWorldViewSize.x * 0.5f,
      gWorldViewSize.y * 0.5f - (worldPoint.y - gViewOffset.y) * gViewZoom);
}

// const ImU32 gColours[] = { 4289639675, 4293119411, 4291161036, 4293184478, 4289124862, 4291624959, 4290631909, 4293712637, 4294111986 };
const ImU32 gColours[] = {4283695428, 4285867080, 4287054913, 4287455029, 4287526954, 4287402273, 4286883874, 4285579076, 4283552122, 4280737725, 4280674301};

void renderBounds(wp::BoundingBox const& bounds, editor::Settings const& settings, ImColor colour, ImDrawList* drawList) {
  wp::Vector2 minExtent, maxExtent;
  bounds.getExtents(minExtent, maxExtent);

  drawList->AddRect(worldToScreen(minExtent), worldToScreen(maxExtent), colour);
}

void renderGrid(float gridSize, wp::BoundingBox const& viewBounds, ImColor const& colour, float width, shared_ptr<bw::core::World> world, ImDrawList* drawList) {
  wp::Vector2 xMinMax, yMinMax;
  viewBounds.getExtents(xMinMax, yMinMax);

  float xMin = xMinMax.x, yMin = xMinMax.y, xMax = yMinMax.x, yMax = yMinMax.y;

  if (world) {
    auto const& worldBounds = world->getExtents();

    wp::Vector2 minExtent, maxExtent;
    worldBounds.getExtents(minExtent, maxExtent);

    xMin = max(xMin, minExtent.x);
    xMax = min(xMax, maxExtent.x);

    yMin = max(yMin, minExtent.y);
    yMax = min(yMax, maxExtent.y);
  }

  // Iterating in world space (rather than pre-computing pixel positions)
  // keeps grid lines correctly spaced and clipped at any zoom level.
  for (float x = ceilf(xMin / gridSize) * gridSize; x <= xMax; x += gridSize) {
    drawList->AddLine(
        worldToScreen({x, yMin}),
        worldToScreen({x, yMax}),
        colour,
        width);
  }

  for (float y = ceilf(yMin / gridSize) * gridSize; y <= yMax; y += gridSize) {
    drawList->AddLine(
        worldToScreen({xMin, y}),
        worldToScreen({xMax, y}),
        colour,
        width);
  }
}

void renderPrimitiveFieldPreview(
    bw::core::PrimitiveFieldLayout const& layout,
    vector<editor::PrimitiveFieldPrimitivePreview> const& primitives,
    ImDrawList* drawList) {
  auto const cellColour = ImColor(0.1f, 0.85f, 1.0f, 0.9f);
  auto const primitiveFillColour = ImColor(0.95f, 0.3f, 0.75f, 0.12f);
  auto const primitiveColour = ImColor(0.95f, 0.3f, 0.75f, 0.72f);
  auto const holeFillColour = ImColor(0.95f, 0.15f, 0.1f, 0.18f);
  auto const holeColour = ImColor(1.0f, 0.2f, 0.1f, 0.9f);
  auto const siteColour = ImColor(1.0f, 0.55f, 0.1f, 1.0f);

  drawList->AddDrawCmd();
  for (auto const& cell : layout.cells) {
    vector<ImVec2> points;
    points.reserve(cell.vertices.size());
    for (auto const& vertex : cell.vertices) {
      points.push_back(worldToScreen(vertex));
    }
    drawList->AddPolyline(
        points.data(), static_cast<int>(points.size()), cellColour,
        ImDrawFlags_Closed, 1.5f);
  }

  drawList->AddDrawCmd();
  for (auto const& primitive : primitives) {
    vector<ImVec2> points;
    points.reserve(primitive.contour.size());
    for (auto const& vertex : primitive.contour) {
      points.push_back(worldToScreen(vertex));
    }
    drawList->AddConvexPolyFilled(
        points.data(), static_cast<int>(points.size()),
        primitive.isHole ? holeFillColour : primitiveFillColour);
    drawList->AddPolyline(
        points.data(), static_cast<int>(points.size()),
        primitive.isHole ? holeColour : primitiveColour, ImDrawFlags_Closed,
        2.0f);
  }

  drawList->AddDrawCmd();
  for (auto const& site : layout.sites) {
    auto screenPoint = worldToScreen(site);
    drawList->AddCircleFilled(screenPoint, 3.5f, siteColour, 12);
    drawList->AddCircle(screenPoint, 5.0f, siteColour, 12, 1.0f);
  }
}

ImColor fadeColour(ImColor colour, float alphaScale) {
  colour.Value.w *= alphaScale;
  return colour;
}

// A closed screen-space outline offset inward, for the band drawn inside the
// ghost. Each vertex moves along the bisector of its two edge normals. The
// bisector is deliberately not scaled by the corner angle: that keeps a sharp
// corner pinching the band inward a little rather than flinging the vertex
// across the shape.
vector<ImVec2> insetOutline(vector<ImVec2> const& points, float distance) {
  auto count = (int)points.size();

  if (count < 3) {
    return points;
  }

  // Screen space flips y, so the authored winding says nothing about which
  // side of an edge faces inward; the signed area has to.
  float twiceArea = 0.0f;
  for (int i = 0; i < count; ++i) {
    auto const& a = points[i];
    auto const& b = points[(i + 1) % count];
    twiceArea += a.x * b.y - b.x * a.y;
  }

  auto inward = twiceArea > 0.0f ? 1.0f : -1.0f;

  auto edgeNormal = [&](int index) {
    auto const& a = points[index];
    auto const& b = points[(index + 1) % count];
    auto dx = b.x - a.x;
    auto dy = b.y - a.y;
    auto length = sqrtf(dx * dx + dy * dy);

    if (length <= 0.0f) {
      return ImVec2{0.0f, 0.0f};
    }

    return ImVec2{-dy / length * inward, dx / length * inward};
  };

  vector<ImVec2> inset(count);

  for (int i = 0; i < count; ++i) {
    auto previous = edgeNormal((i + count - 1) % count);
    auto next = edgeNormal(i);

    auto bx = previous.x + next.x;
    auto by = previous.y + next.y;
    auto length = sqrtf(bx * bx + by * by);

    // The two normals cancel on a spike; the edge's own normal still points
    // the right way there.
    if (length <= 1e-4f) {
      bx = next.x;
      by = next.y;
      length = sqrtf(bx * bx + by * by);
    }

    inset[i] = length > 0.0f
                   ? ImVec2{points[i].x + bx / length * distance,
                            points[i].y + by / length * distance}
                   : points[i];
  }

  return inset;
}

// Which of a Layer's built Primitives come from a step other than its active
// one, indexed by Primitive id - the id being its position in that built
// collection, which is also what an arrangement face reports as the Primitive
// it belongs to.
//
// Built by walking the steps once. Asking each Primitive for its owning step
// instead would rescan every step's field for every Primitive, every frame.
vector<uint8_t> collectInactiveStepPrimitives(bw::core::Layer const& layer) {
  vector<uint8_t> flags(layer.getNumPrimitives(), 0);

  auto activeStepIndex = layer.getActiveStepIndex();
  auto numSteps = layer.getNumSteps();

  for (uint32_t stepIndex = 0; stepIndex < numSteps; ++stepIndex) {
    if (stepIndex == activeStepIndex) {
      continue;
    }

    auto const* field =
        dynamic_cast<bw::core::PrimitiveField const*>(layer.getStep(stepIndex));

    if (!field) {
      continue;
    }

    for (auto const* primitive : field->getPrimitives()) {
      auto id = primitive->getId();

      // A disabled step's Primitives never reached the built collection, so
      // the ids they still carry belong to somebody else.
      if (id < flags.size() && layer.getPrimitive(id) == primitive) {
        flags[id] = 1;
      }
    }
  }

  return flags;
}

bool fromInactiveStep(vector<uint8_t> const& flags, uint32_t primitiveId) {
  return primitiveId < flags.size() && flags[primitiveId] != 0;
}

void renderWorld(
    editor::Document* doc,
    editor::Settings const& settings,
    bw::core::ArrangementWorldData const* worldData,
    double globalTime) {
  auto world = doc->getWorld();

  bool renderWorldStuff = world != nullptr;

  // Triangulate, etc
  auto visibleWorldSize = gWorldViewSize / gViewZoom;
  wp::BoundingBox viewBounds(gViewOffset - visibleWorldSize / 2, visibleWorldSize);

  // Get all primitives for now
  vector<bw::core::Primitive*> primitives;
  vector<bw::core::WorldTriggerLine*> triggerLines;

  if (world) {
    primitives = world->findPrimitives(viewBounds);
    triggerLines = world->findTriggerLines(viewBounds);
  }

  // The same rule the world data generator folds by (see
  // editor::primitiveVisibleForActiveStep) drops the overlay for a hidden
  // Primitive here: the generator answers for the geometry, and the Layer's
  // lookup grid - which this list comes from - knows nothing of either.
  auto* activeLayer = world ? world->getActiveLayer() : nullptr;

  auto inactiveStepPrimitives =
      activeLayer ? collectInactiveStepPrimitives(*activeLayer) : vector<uint8_t>{};

  if (activeLayer) {
    primitives.erase(
        remove_if(primitives.begin(), primitives.end(),
            [&](bw::core::Primitive const* primitive) {
              return !editor::primitiveVisibleForActiveStep(
                  *activeLayer, primitive, settings);
            }),
        primitives.end());
  }

  auto numPrimitives = (uint32_t)primitives.size();
  bool renderPrimitiveStuff = numPrimitives > 0;

  // Render
  auto drawList = ImGui::GetWindowDrawList();

  if (renderPrimitiveStuff) {
    auto const& arrangement = worldData->getArrangement();
    auto const& triangles = worldData->getTriangles();
    auto toWorld = [&](uint32_t vertexIndex) {
      auto const& vertex = arrangement.vertices[vertexIndex];
      return wp::Vector2{
          bw::core::arr::ToWorldCoordinate(vertex.x),
          bw::core::arr::ToWorldCoordinate(vertex.y)};
    };

    auto const& selectedPrimitiveIndices = doc->getSelectedPrimitiveIndices();

    if (!triangles.empty()) {
      drawList->AddDrawCmd();
      drawList->Flags &= ~ImDrawListFlags_AntiAliasedFill;

      // The geometry a Primitive contributes is the bulk of what it looks
      // like, so it carries the fade too - a border-only fade is invisible
      // under a fill drawn at full strength. A face names the Primitive that
      // won the fold over it, which is the one whose step it belongs to.
      auto faceFaded = [&](uint32_t faceIndex) {
        return fromInactiveStep(
            inactiveStepPrimitives, arrangement.faces[faceIndex].primitiveIndex);
      };

      for (auto const& triangle : triangles) {
        auto v0 = toWorld(triangle.v[0]);
        auto v1 = toWorld(triangle.v[1]);
        auto v2 = toWorld(triangle.v[2]);
        auto faded = faceFaded(triangle.face);
        if (settings.renderTriangulation) {
          drawList->AddTriangle(
              worldToScreen(v0),
              worldToScreen(v1),
              worldToScreen(v2),
              faded
                  ? fadeColour(settings.triangulationColour, ED_INACTIVE_STEP_PRIMITIVE_ALPHA_SCALE)
                  : settings.triangulationColour);
        } else {
          drawList->AddTriangleFilled(
              worldToScreen(v0),
              worldToScreen(v1),
              worldToScreen(v2),
              faded
                  ? fadeColour(settings.backgroundColour, ED_INACTIVE_STEP_PRIMITIVE_ALPHA_SCALE)
                  : settings.backgroundColour);
        }
      }

      if (settings.renderWorldBorder) {
        drawList->AddDrawCmd();
        for (auto const& wall : worldData->getWalls()) {
          if (wall.kind != bw::core::arr::ArrangementWallKind::Border) {
            continue;
          }
          auto const& edge = arrangement.edges[wall.edge];
          auto v0 = toWorld(edge.v[0]);
          auto v1 = toWorld(edge.v[1]);
          drawList->AddLine(
              worldToScreen(v0),
              worldToScreen(v1),
              settings.borderColour,
              3.0f);
        }
      }
    }

    // Primitives
    if (!primitives.empty()) {
      drawList->AddDrawCmd();

      // The ghost's contours, held back until every Primitive has been drawn
      // so that it lands on top of all of them rather than only on top of
      // itself. It keeps every contour it has: dropping all but the last
      // would hide its holes and disjoint pieces.
      vector<vector<ImVec2>> ghostBorderPolylines;

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

        // A Primitive from any step but the active one renders faded, so
        // the active step's contribution stands out among what's shown.
        bool faded = !isGhost && fromInactiveStep(inactiveStepPrimitives, primitiveId);

        auto primitiveColour = faded
                                    ? fadeColour(settings.primitiveColour, ED_INACTIVE_STEP_PRIMITIVE_ALPHA_SCALE)
                                    : settings.primitiveColour;

        // Primitive borders
        if (settings.renderPrimitiveBorders || isGhost || selected) {
          auto complexPolygons = primitive->getVertices();

          for (auto const& complexPolygon : complexPolygons) {
            for (auto const& polygon : complexPolygon) {
              auto numVertices = (int)polygon.size();
              vector<ImVec2> imPoints(numVertices);

              for (int i = 0; i < numVertices; ++i) {
                imPoints[i] = worldToScreen(polygon[i].p);
              }

              if (isGhost) {
                // Defer ghost to end, so that its lines are always visible
                ghostBorderPolylines.push_back(move(imPoints));
              } else if (selected) {
                drawList->AddPolyline(imPoints.data(), numVertices, settings.selectedPrimitiveColour, ImDrawFlags_Closed, 2.5f);
              } else {
                drawList->AddPolyline(imPoints.data(), numVertices, primitiveColour, ImDrawFlags_Closed, 1.5f);
              }
            }
          }
        }

      }

      // The ghost, over every Primitive and in its own colour, always: it is
      // authoring furniture rather than world content, so neither the step
      // fade nor a selection restyles it, and nothing draws over it.
      for (auto const& ghostOutline : ghostBorderPolylines) {
        auto band = insetOutline(ghostOutline, ED_GHOST_INNER_BAND_INSET);

        drawList->AddPolyline(
            band.data(),
            (int)band.size(),
            fadeColour(settings.ghostPrimitiveColour, ED_GHOST_INNER_BAND_ALPHA_SCALE),
            ImDrawFlags_Closed,
            ED_GHOST_INNER_BAND_THICKNESS);

        drawList->AddPolyline(
            ghostOutline.data(),
            (int)ghostOutline.size(),
            settings.ghostPrimitiveColour,
            ImDrawFlags_Closed,
            ED_GHOST_BORDER_THICKNESS);
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

          // Influence centre. Kept as a screen-space point so the fixed-size
          // eye decoration below (radius/offsets in constant screen pixels)
          // stays a legible, constant size regardless of zoom.
          auto influenceCentreScreen = worldToScreen(primitive->getInfluenceEyeOriginPosition());
          wp::Vector2 influenceCentre{influenceCentreScreen.x, influenceCentreScreen.y};

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
    // Arrangement vertices
    //
    if (settings.renderArrangementVertices) {
      for (auto const& vertex : arrangement.vertices) {
        wp::Vector2 p{
            bw::core::arr::ToWorldCoordinate(vertex.x),
            bw::core::arr::ToWorldCoordinate(vertex.y)};
        auto screenP = worldToScreen(p);
        drawList->AddCircle(
            screenP, settings.vertexRadius, settings.vertexColour, 16, 1.0f);
      }
    }

    //
    // Player start
    //
    auto playerStartPos = world->getPlayerStartPosition();
    auto playerStartAngle = world->getPlayerStartAngle();
    auto startLookPos = playerStartPos + wp::Vector2(0, 1).rotatedCopy(playerStartAngle) * (BW_PLAYER_RADIUS + 20);

    auto playerStartScreen = worldToScreen(playerStartPos);
    auto startLookScreen = worldToScreen(startLookPos);

    drawList->AddCircle(playerStartScreen, (BW_PLAYER_RADIUS + 20) * gViewZoom, ImColor(0, 1, 0), BW_PLAYER_RADIUS * 2, 2);

    drawList->AddLine(playerStartScreen, startLookScreen, ImColor(0, 1, 0), 2);

    //
    // Player proxy
    //
    drawList->AddDrawCmd();

    auto playerProxyPos = doc->getPlayerProxyPosition();
    float playerProxyAngle = doc->getPlayerProxyAngle();

    auto playerProxyScreen = worldToScreen(playerProxyPos);

    if (settings.renderPlayerView) {
      drawList->AddCircleFilled(playerProxyScreen, BW_PLAYER_VIEW_DISTANCE * gViewZoom, ImColor(1.0f, 1.0f, 1.0f, 0.3f));
    }

    drawList->AddCircleFilled(playerProxyScreen, BW_PLAYER_RADIUS * gViewZoom, settings.playerProxyColour);

    // FOV - flip Y values
    if (settings.renderPlayerView) {
      auto v0 = playerProxyPos;
      auto [v1, v2] = bw::core::calculateFovTriangle(v0, playerProxyAngle - 180, BW_PLAYER_VIEW_DISTANCE, BW_PLAYER_FOV);

      drawList->AddTriangleFilled(worldToScreen(v0), worldToScreen(v1), worldToScreen(v2), ImColor(0.0f, 0.5f, 0.7f, 0.4f));
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
              int numSegs = (int)(tuDist / 8);
              drawList->AddCircle(worldToScreen(primPosition), tuDist * gViewZoom, settings.timeUpdateDistColour, numSegs, 1.5f);
            }
          }

          auto influenceCentre = worldToScreen(primitive->getInfluenceEyeOriginPosition());

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
              float radius = (scaleMax.x - point.first) * gViewZoom;
              drawList->AddCircle(influenceCentre, radius, colour, numSegs, 1.5f);
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

        renderBounds(primitive->getBounds(), settings, settings.animatedBoundsColour, drawList);
      }
    }
  }

  // Triggers
  if (settings.renderTriggerLines) {
    for (auto triggerLine : triggerLines) {
      wp::Vector2 points[2];

      points[0] = triggerLine->getPoint(0);
      points[1] = triggerLine->getPoint(1);

      auto p0Screen = worldToScreen(points[0]);
      auto p1Screen = worldToScreen(points[1]);
      wp::Vector2 p0{p0Screen.x, p0Screen.y};
      wp::Vector2 p1{p1Screen.x, p1Screen.y};

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
    renderGrid(settings.gridSize, viewBounds, settings.gridColour, 1.0f, renderWorldStuff ? world : nullptr, drawList);
  }

  // Primitive-field layout preview. This editor-only overlay is deliberately
  // rendered after authored geometry and is never added to World.
  auto const& primitiveFieldPreview = editor::getPrimitiveFieldPreview();
  if (primitiveFieldPreview.open && primitiveFieldPreview.layout) {
    renderPrimitiveFieldPreview(
        *primitiveFieldPreview.layout, primitiveFieldPreview.primitives,
        drawList);
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
  auto world = doc->getWorld();
  auto worldBounds = world->getExtents();
  auto worldSize = worldBounds.getSize() / MINIMAP_SCALE;

  return wp::BoundingBox(MINIMAP_PADDING, MINIMAP_PADDING, worldSize.x, worldSize.y);
}

void renderMiniMap(editor::Document* doc, editor::Settings const& settings, bw::core::WorldData const* worldData, double globalTime) {
  auto windowSize = gWorldViewSize;

  // Render
  auto drawList = ImGui::GetWindowDrawList();

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

    drawList->AddRectFilled(toScreen(pb0), toScreen(pb1), fColour);
    drawList->AddRect(toScreen(pb0), toScreen(pb1), bColour);
  }

  // View window
  auto visibleWorldSize = windowSize / gViewZoom;
  wp::BoundingBox viewBounds(gViewOffset - visibleWorldSize / 2, visibleWorldSize);

  auto b0 = worldToMinimap(viewBounds.getMinExtent(), worldBounds, mmBounds);
  auto b1 = worldToMinimap(viewBounds.getMaxExtent(), worldBounds, mmBounds);

  drawList->AddRect(toScreen(b0), toScreen(b1), ImColor(1.0f, 0.5f, 0.0f));

  // Border
  drawList->AddRect(toScreen(mmMin), toScreen(mmMax), ImColor(1.0f, 0.0f, 0.0f));
}