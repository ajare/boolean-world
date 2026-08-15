#pragma once

#include <vector>

#include "willpower/common/Vector2.h"

#include "willpower/firepower/Platform.h"

namespace WP_NAMESPACE {
namespace firepower {

struct Vertex {
  wp::Vector2 p, n;
  uint32_t e[2];
};

struct Edge {
  uint32_t vi[2];
  wp::Vector2 v[2], n;
};

struct Mesh {
  std::vector<Vertex> vertices;
  std::vector<Edge> edges;
};

}  // namespace firepower
}  // namespace WP_NAMESPACE

#pragma once
