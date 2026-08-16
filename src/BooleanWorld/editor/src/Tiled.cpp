#include <fstream>

#include <nlohmann/json.hpp>

#pragma warning(push)
#pragma warning(disable : 4307)
#include <spdlog/spdlog.h>
#pragma warning(pop)

#include <willpower/common/MathsUtils.h>

#include "core/Vertex.h"
#include "core/MeshPrimitive.h"

#include "Tiled.h"

#define SIDE_TOP 1
#define SIDE_RIGHT 2
#define SIDE_BOTTOM 3
#define SIDE_LEFT 4

extern spdlog::logger* gLogger;

using namespace std;

struct BorderLine {
  int indices[2];
  vector<wp::Vector2> inter;

public:
  static BorderLine straight(int i0, int i1) {
    return {{i0, i1}, {}};
  }

  static BorderLine complex(int i0, int i1, vector<wp::Vector2> const& interPoints) {
    return {{i0, i1}, interPoints};
  }

  static BorderLine curve(int i0, int i1, int c, int width) {
    int x0 = i0 % width;
    int y0 = i0 / width;
    int x1 = i1 % width;
    int y1 = i1 / width;
    int cx = c % width;
    int cy = c / width;

    auto pc = wp::Vector2(x0 - cx, y0 - cy);

    vector<wp::Vector2> interPoints;
    for (int i = 1; i < 8; ++i) {
      // Generate points from 0 to 1 around line.c
      auto p = pc.rotatedAnticlockwiseCopy(90.0f * i / 8.0f);

      p.x += cx;
      p.y += cy;

      interPoints.push_back({p});
    }

    return {{i0, i1}, interPoints};
  }
};

bool hasSide(int tileId, int side) {
  if (tileId == 0) {
    return false;
  }

  tileId = (tileId - 1) / 4;

  switch (tileId) {
    case 0:
      return true;

    case 1:
    case 5:
      return side == SIDE_BOTTOM || side == SIDE_LEFT;

    case 2:
    case 6:
      return side == SIDE_TOP || side == SIDE_LEFT;

    case 3:
    case 7:
      return side == SIDE_TOP || side == SIDE_RIGHT;

    case 4:
    case 8:
      return side == SIDE_RIGHT || side == SIDE_BOTTOM;

    case 9:
    case 10:
      return true;

    case 11:
      return side == SIDE_BOTTOM;

    case 12:
      return side == SIDE_LEFT;

    case 13:
      return side == SIDE_TOP;

    case 14:
      return side == SIDE_RIGHT;

    default:
      return false;
  }
}

wp::Vector2 transV(wp::Vector2 const& v) {
  auto vt = v;

  vt *= 32;
  vt -= 256;
  vt.y = -vt.y;

  return vt;
}

wp::Vector2 transV(float x, float y) {
  return transV({x, y});
}

bw::core::ClosedPolygon generateSquare(int x, int y, float inset) {
  const float inseti = 1.0f - inset;
  bw::core::ClosedPolygon res;

  res.push_back({transV(x + inset, y + inset)});
  res.push_back({transV(x + inset, y + inseti)});
  res.push_back({transV(x + inseti, y + inseti)});
  res.push_back({transV(x + inseti, y + inset)});

  return res;
}

bw::core::ClosedPolygon generateOctagon(int x, int y, float majDim, float minDim) {
  const float majDim2 = majDim / 2;
  const float minDim2 = minDim / 2;
  bw::core::ClosedPolygon res;

  float xp = x + 0.5f;
  float yp = y + 0.5f;

  res.push_back({transV(xp - minDim2, yp - majDim2)});
  res.push_back({transV(xp - majDim2, yp - minDim2)});
  res.push_back({transV(xp - majDim2, yp + minDim2)});
  res.push_back({transV(xp - minDim2, yp + majDim2)});
  res.push_back({transV(xp + minDim2, yp + majDim2)});
  res.push_back({transV(xp + majDim2, yp + minDim2)});
  res.push_back({transV(xp + majDim2, yp - minDim2)});
  res.push_back({transV(xp + minDim2, yp - majDim2)});

  return res;
}

bw::core::ClosedPolygon removeCollinearPoints(bw::core::ClosedPolygon const& poly) {
  if (poly.size() < 3) {
    return poly;
  }

  bw::core::ClosedPolygon res;
  res.reserve(poly.size());

  res.push_back(poly[0]);
  res.push_back(poly[1]);

  for (uint32_t i = 2; i < (uint32_t)poly.size(); ++i) {
    auto c = res.size();
    bool push = true;

    while (wp::MathsUtils::pointsFormLine(res[c - 2].p, res[c - 1].p, poly[i].p)) {
      res[c - 1] = poly[i];
      i++;

      if (i == (uint32_t)poly.size()) {
        push = false;
        break;
      }
    }

    if (push) {
      res.push_back(poly[i]);
    }
  }

  auto c = res.size();

  if (wp::MathsUtils::pointsFormLine(res[c - 2].p, res[c - 1].p, res[0].p)) {
    res.pop_back();
  }

  return res;
}

void openTiledPrefabFile(string const& filepath, shared_ptr<bw::core::World> world) {
  ifstream f(filepath);

  auto j = nlohmann::json::parse(f);

  auto const& layers = j["layers"];
  uint32_t tileLayerIndex{0};

  for (auto const& layer : layers) {
    auto const& layerType = layer["type"].get<string>();
    auto const& layerName = layer["name"].get<string>();

    bw::core::Primitive::Operation op{bw::core::Primitive::Operation::Union};
    uint8_t priority = (uint8_t)tileLayerIndex;

    if (layer.contains("properties")) {
      for (auto const& property : layer["properties"]) {
        auto const& propName = property["name"].get<string>();

        if (propName == "operation") {
          auto const& propValue = property["value"].get<string>();

          if (propValue == "union") {
            op = bw::core::Primitive::Operation::Union;
          } else if (propValue == "difference") {
            op = bw::core::Primitive::Operation::Difference;
          } else if (propValue == "intersection") {
            op = bw::core::Primitive::Operation::Intersection;
          } else if (propValue == "xor") {
            op = bw::core::Primitive::Operation::XOR;
          }
        } else if (propName == "priority") {
          auto propValue = property["value"].get<int>();
          priority = (uint8_t)propValue;
        }
      }
    }

    if (layerType == "tilelayer") {
      // Build up contiguous blocks of the same type and convert them
      // to MeshPrimitives, except for those on their own, which will
      // become RectanglePrimitives.
      // Tiles come in families.  Ids 1, 5, 9 etc are one family. 2, 6, 10 another.
      auto layerWidth = layer["width"].get<int>();
      auto layerHeight = layer["height"].get<int>();

      vector<int> lookup(layerWidth * layerHeight, -1);
      vector<vector<int>> groups;
      auto const& data = layer["data"].get<vector<int>>();

      for (uint32_t i = 0; i < (uint32_t)data.size(); ++i) {
        auto tileId = data[i];

        if (tileId == 0) {
          continue;
        }

        int tileFamily = tileId % 4;
        int x = i % layerWidth;
        int y = i / layerWidth;
        int leftGroup = -1, topGroup = -1;

        // Fully open block
        if (x > 0) {
          // Check left
          if ((data[i - 1] % 4) == tileFamily) {
            leftGroup = lookup[i - 1];
          }
        }
        if (y > 0) {
          // Check top
          if ((data[i - layerWidth] % 4) == tileFamily) {
            topGroup = lookup[i - layerWidth];
          }
        }

        if (leftGroup >= 0 && topGroup < 0) {
          // Add to left group
          lookup[i] = leftGroup;
          groups[leftGroup].push_back({(int)i});
        } else if (leftGroup < 0 && topGroup >= 0) {
          // Add to top group
          lookup[i] = topGroup;
          groups[topGroup].push_back({(int)i});
        } else if (leftGroup >= 0 && topGroup >= 0) {
          if (leftGroup == topGroup) {
            // Add to either (eg left)
            lookup[i] = leftGroup;
            groups[leftGroup].push_back({(int)i});
          } else {
            // Merge groups (into top)
            for (auto groupI : groups[leftGroup]) {
              groups[topGroup].push_back(groupI);
              lookup[groupI] = topGroup;
            }

            groups[leftGroup].clear();

            // Don't forget to add this tile
            lookup[i] = topGroup;
            groups[topGroup].push_back((int)i);
          }
        } else {
          // New group
          lookup[i] = (int)groups.size();
          groups.push_back({(int)i});
        }
      }

      // At this point, convert each group to a Primitive
      int vertexGridWidth = layerWidth + 1;
      int vertexGridHeight = layerHeight + 1;

      for (auto const& group : groups) {
        bw::core::ComplexPolygon polygons;

        map<int, BorderLine> borderLineMap;

        for (int index : group) {
          auto tileId = data[index];
          auto tileFamily = tileId % 4;
          tileId = (tileId - 1) / 4;

          int x = index % layerWidth;
          int y = index / layerWidth;

          // Vertex grid indices
          int v00 = y * vertexGridWidth + x;
          int v10 = y * vertexGridWidth + x + 1;
          int v01 = (y + 1) * vertexGridWidth + x;
          int v11 = (y + 1) * vertexGridWidth + x + 1;

          // Generate edges clockwise for this type,
          // based on its neighbours
          switch (tileId) {
            case 0:  // filled
              if (y == 0 || !hasSide(data[index - layerWidth], SIDE_BOTTOM)) {
                borderLineMap[v00] = BorderLine::straight(v00, v10);
              }
              if (x == (layerWidth - 1) || !hasSide(data[index + 1], SIDE_LEFT)) {
                borderLineMap[v10] = BorderLine::straight(v10, v11);
              }
              if (y == (layerHeight - 1) || !hasSide(data[index + layerWidth], SIDE_TOP)) {
                borderLineMap[v11] = BorderLine::straight(v11, v01);
              }
              if (x == 0 || !hasSide(data[index - 1], SIDE_RIGHT)) {
                borderLineMap[v01] = BorderLine::straight(v01, v00);
              }
              break;

            case 1:  // filled bottom-left diag
            case 5:  // filled bottom-left circle
              borderLineMap[v00] = tileId == 1 ? BorderLine::straight(v00, v11) : BorderLine::curve(v00, v11, v01, vertexGridWidth);

              if (y == (layerHeight - 1) || !hasSide(data[index + layerWidth], SIDE_TOP)) {
                borderLineMap[v11] = BorderLine::straight(v11, v01);
              }
              if (x == 0 || !hasSide(data[index - 1], SIDE_RIGHT)) {
                borderLineMap[v01] = BorderLine::straight(v01, v00);
              }
              break;

            case 2:  // filled top-left diag
            case 6:  // filled top-left circle
              borderLineMap[v10] = tileId == 2 ? BorderLine::straight(v10, v01) : BorderLine::curve(v10, v01, v00, vertexGridWidth);

              if (x == 0 || !hasSide(data[index - 1], SIDE_RIGHT)) {
                borderLineMap[v01] = BorderLine::straight(v01, v00);
              }
              if (y == 0 || !hasSide(data[index - layerWidth], SIDE_BOTTOM)) {
                borderLineMap[v00] = BorderLine::straight(v00, v10);
              }
              break;

            case 3:  // filled top-right diag
            case 7:  // filled top-right circle
              borderLineMap[v11] = tileId == 3 ? BorderLine::straight(v11, v00) : BorderLine::curve(v11, v00, v10, vertexGridWidth);

              if (y == 0 || !hasSide(data[index - layerWidth], SIDE_BOTTOM)) {
                borderLineMap[v00] = BorderLine::straight(v00, v10);
              }
              if (x == (layerWidth - 1) || !hasSide(data[index + 1], SIDE_LEFT)) {
                borderLineMap[v10] = BorderLine::straight(v10, v11);
              }
              break;

            case 4:  // filled bottom-right diag
            case 8:  // filled bottom-right circle
              borderLineMap[v01] = tileId == 4 ? BorderLine::straight(v01, v10) : BorderLine::curve(v01, v10, v11, vertexGridWidth);

              if (x == (layerWidth - 1) || !hasSide(data[index + 1], SIDE_LEFT)) {
                borderLineMap[v10] = BorderLine::straight(v10, v11);
              }
              if (y == (layerHeight - 1) || !hasSide(data[index + layerWidth], SIDE_TOP)) {
                borderLineMap[v11] = BorderLine::straight(v11, v01);
              }
              break;

            case 9:  // Square pillar (internal)
              polygons.push_back(generateSquare(x, y, 0.25));
              break;

            case 10:  // Octagonal pillar (internal)
              polygons.push_back(generateOctagon(x, y, 0.75f, 0.5f));
              break;

            case 11:  // Bottom triangle
              if (y == (layerHeight - 1) || !hasSide(data[index + layerWidth], SIDE_TOP)) {
                borderLineMap[v11] = BorderLine::straight(v11, v01);
              }

              borderLineMap[v01] = BorderLine::complex(v01, v11, {{x + 0.5f, y + 0.5f}});
              break;

            case 12:  // Left triangle
              if (x == 0 || !hasSide(data[index - 1], SIDE_RIGHT)) {
                borderLineMap[v01] = BorderLine::straight(v01, v00);
              }

              borderLineMap[v00] = BorderLine::complex(v00, v01, {{x + 0.5f, y + 0.5f}});
              break;

            case 13:  // Top triangle
              if (y == 0 || !hasSide(data[index - layerWidth], SIDE_BOTTOM)) {
                borderLineMap[v00] = BorderLine::straight(v00, v10);
              }

              borderLineMap[v10] = BorderLine::complex(v10, v00, {{x + 0.5f, y + 0.5f}});
              break;

            case 14:  // Right triangle
              if (x == (layerWidth - 1) || !hasSide(data[index + 1], SIDE_LEFT)) {
                borderLineMap[v10] = BorderLine::straight(v10, v11);
              }

              borderLineMap[v11] = BorderLine::complex(v11, v10, {{x + 0.5f, y + 0.5f}});
              break;
          }
        }

        // Go through border map.  There may be more than one loop: one clockwise one,
        // and zero or more anticlockwise ones (holes).
        while (!borderLineMap.empty()) {
          bw::core::ClosedPolygon polygon;

          auto indices = *(borderLineMap.begin());
          auto [firstIndex, line] = indices;

          // Initial point
          int x = firstIndex % vertexGridWidth;
          int y = firstIndex / vertexGridWidth;

          polygon.push_back({transV(x, y)});

          while (true) {
            // Intermediate points
            for (auto const& inter : line.inter) {
              polygon.push_back({transV(inter)});
            }

            // Next
            auto newIndex = line.indices[1];
            borderLineMap.erase(line.indices[0]);

            if (newIndex == firstIndex) {
              break;
            }

            line = borderLineMap[newIndex];

            // Final point
            int x = line.indices[0] % vertexGridWidth;
            int y = line.indices[0] / vertexGridWidth;

            polygon.push_back({transV(x, y)});
          }

          polygon = removeCollinearPoints(polygon);
          polygons.push_back(polygon);
        }

        // At this point, polygons represents the loops required
        // for a single MeshPrimitive.
        if (!polygons.empty()) {
          auto meshPoly = new bw::core::MeshPrimitive(op, bw::core::Primitive::FillRule::EvenOdd, {polygons});

          meshPoly->setSize({2.0f, 2.0f});
          meshPoly->setPriority(priority);

          world->addPrimitive(meshPoly);
        }
      }

      tileLayerIndex++;
    }
  }
}