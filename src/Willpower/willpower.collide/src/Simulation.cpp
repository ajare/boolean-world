#include <algorithm>
#include <cassert>

#include <utils/StringUtils.h>

#include "willpower/common/MathsUtils.h"

#include "willpower/collide/Simulation.h"

using namespace std;

namespace WP_NAMESPACE {
namespace collide {

using namespace utils;

Simulation::Simulation(ExtentsCalculator const& extents, uint32_t cellsX, uint32_t cellsY, void* userObj)
    : mNextIndex(0), mCollidersGrid(nullptr), mStaticLinesGrid(nullptr), mwUserObject(userObj), mMinExtent(1e10, 1e10), mMaxExtent(-1e10, -1e10), mNumSweepChecks(0) {
  auto cellSize = extents.getCellSize(cellsX, cellsY);
  createGrids(extents.getMinExtent(), extents.getMaxExtent(), cellSize.x, cellSize.y);
}

Simulation::~Simulation() {
  destroyGrids();
  destroyColliders();
}

void Simulation::destroyColliders() {
  for (auto collider : mColliders) {
    delete collider;
  }

  mColliders.clear();
}

void Simulation::getExtents(Vector2& minExtent, Vector2& maxExtent) {
  minExtent = mMinExtent;
  maxExtent = mMaxExtent;
}

void Simulation::createGrids(Vector2 const& minExtent, Vector2 const& maxExtent, float cellSizeX, float cellSizeY) {
  destroyGrids();

  // Calculate cell dimensions from size
  Vector2 size = maxExtent - minExtent;

  int cellDimX = (int)(size.x / (float)cellSizeX) + 1;
  int cellDimY = (int)(size.y / (float)cellSizeY) + 1;

  float newSizeX = cellDimX * cellSizeX;
  float newSizeY = cellDimY * cellSizeY;

  float dSizeX2 = (newSizeX - size.x) / 2.0f;
  float dSizeY2 = (newSizeY - size.y) / 2.0f;

  mCollidersGrid = new AccelerationGrid(minExtent.x - dSizeX2, minExtent.y - dSizeY2, newSizeX, newSizeY, cellDimX, cellDimY);
  mStaticLinesGrid = new AccelerationGrid(minExtent.x - dSizeX2, minExtent.y - dSizeY2, newSizeX, newSizeY, cellDimX, cellDimY);
}

void Simulation::destroyGrids() {
  delete mCollidersGrid;
  mCollidersGrid = nullptr;

  delete mStaticLinesGrid;
  mStaticLinesGrid = nullptr;
}

int32_t Simulation::addCollider(unique_ptr<Collider> collider) {
  uint32_t colliderIndex = mNextIndex++;
  auto colliderObserver = collider.get();
  colliderObserver->_setIndex(colliderIndex);

  mColliders.push_back(colliderObserver);

  // Add to grid
  try {
    mCollidersGrid->addItem((uint32_t)colliderIndex, colliderObserver->getBounds());
  } catch (...) {
    mColliders.pop_back();
    throw;
  }

  collider.release();
  return colliderIndex;
}

void Simulation::removeCollider(Collider* collider) {
  auto colliderIt = find(mColliders.begin(), mColliders.end(), collider);
  assert(colliderIt != mColliders.end());
  mColliders.erase(colliderIt);

  // Remove from grid
  mCollidersGrid->removeItem((uint32_t)collider->getIndex());

  delete collider;
}

size_t Simulation::getNumColliders() const {
  return mColliders.size();
}

vector<Collider*> Simulation::getColliders() const {
  vector<Collider*> colliders;

  copy(mColliders.begin(), mColliders.end(), back_inserter(colliders));
  return colliders;
}

pair<uint32_t, uint32_t> Simulation::addStaticLine(float x0, float y0, float x1, float y1, bool doubleSided) {
  uint32_t firstItem = (uint32_t)mStaticLines.size();
  uint32_t numItems = 0;

  Vector2 lineMin(min(x0, x1), min(y0, y1));
  Vector2 lineMax(max(x0, x1), max(y0, y1));

  // Update extents
  mMinExtent.x = min(mMinExtent.x, lineMin.x);
  mMinExtent.y = min(mMinExtent.y, lineMin.y);
  mMaxExtent.x = max(mMaxExtent.x, lineMax.x);
  mMaxExtent.y = max(mMaxExtent.y, lineMax.y);

  // Split based on acceleration grid size.
  Vector2 linev0(x0, y0), linev1(x1, y1);

  int cellDimX = mStaticLinesGrid->getCellDimensionX();
  int cellDimY = mStaticLinesGrid->getCellDimensionY();
  Vector2 cellSize = mStaticLinesGrid->getCellSize();
  Vector2 gridOffset = mStaticLinesGrid->getOffset();

  int cellX0, cellY0, cellX1, cellY1;
  cellX0 = (int)((lineMin.x - gridOffset.x) / cellSize.x);
  cellY0 = (int)((lineMin.y - gridOffset.y) / cellSize.y);
  cellX1 = (int)((lineMax.x - gridOffset.x) / cellSize.x);
  cellY1 = (int)((lineMax.y - gridOffset.y) / cellSize.y);

  // Clamp to the grid.  The only time it would be expected to go
  // outside is when it lies directly on an exterior gridline.
  cellX0 = max(0, min(cellX0, cellDimX - 1));
  cellY0 = max(0, min(cellY0, cellDimY - 1));
  cellX1 = max(0, min(cellX1, cellDimX - 1));
  cellY1 = max(0, min(cellY1, cellDimY - 1));

  for (int y = cellY0; y <= cellY1; ++y) {
    for (int x = cellX0; x <= cellX1; ++x) {
      Vector2 cellMin(x * cellSize.x, y * cellSize.y);
      Vector2 cellMax((x + 1) * cellSize.x, (y + 1) * cellSize.y);

      cellMin += gridOffset;
      cellMax += gridOffset;

      LineHit hit1, hit2;
      auto intersect = MathsUtils::lineBoxIntersection(linev0, linev1, cellMin, cellMax, &hit1, &hit2);

      // Keep a complete segment as the safe fallback for intersection results
      // that do not provide a clipping hit.
      Vector2 startPoint = linev0;
      Vector2 endPoint = linev1;
      bool intersecting = false;
      switch (intersect) {
        case MathsUtils::LineIntersectionType::Intersecting:
          // Create a line from either entry point to end point, or start point
          // to exit point.
          switch (hit1.getFlags()) {
            case LineHit::Flags::None:
              switch (hit2.getFlags()) {
                case LineHit::Flags::HitEnters:
                  startPoint = hit2.getPosition();
                  endPoint = linev1;
                  break;

                case LineHit::Flags::HitExits:
                  startPoint = linev0;
                  endPoint = hit2.getPosition();
                  break;

                default:
                  break;
              }
              break;

            case LineHit::Flags::HitEnters:
              startPoint = hit1.getPosition();
              endPoint = linev1;
              break;

            case LineHit::Flags::HitExits:
              startPoint = linev0;
              endPoint = hit1.getPosition();
              break;

            default:
              break;
          }

          intersecting = true;
          break;

        case MathsUtils::LineIntersectionType::DoublyIntersecting:
          // Create a line from entry point to exit point
          startPoint = hit1.getPosition();
          endPoint = hit2.getPosition();
          intersecting = true;
          break;

        case MathsUtils::LineIntersectionType::Inside:
        case MathsUtils::LineIntersectionType::Coincident:
          // Keep lines wholly inside or coincident with a cell boundary.
          intersecting = true;
          break;

        case MathsUtils::LineIntersectionType::Touching:
        case MathsUtils::LineIntersectionType::NotIntersecting:
        default:
          break;
      }

      // Point-only contacts do not form static lines.
      if (intersecting && startPoint != endPoint) {
        auto item = (uint32_t)mStaticLines.size();
        mStaticLines.push_back(StaticLine(startPoint, endPoint, doubleSided, 1.0f));
        mStaticLinesGrid->addItem(item, BoundingBox(startPoint.lerp(endPoint, 0.5f), Vector2::ZERO));
        numItems++;
      }
    }
  }

  return make_pair(firstItem, numItems);
}

void Simulation::addMesh(geometry::Mesh const* mesh) {
  addMesh(mesh, [](geometry::Mesh const* m, uint32_t edgeIndex) {
    auto const& edge = m->getEdge(edgeIndex);
    return edge.getConnectivity() == geometry::Edge::Connectivity::External;
  });
}

void Simulation::addMesh(geometry::Mesh const* mesh, geometry::Mesh::EdgeFilterFunction edgeFilterFn) {
  uint32_t polygonIndex = mesh->getFirstPolygonIndex();
  while (!mesh->polygonIndexIterationFinished(polygonIndex)) {
    geometry::Polygon const& polygon = mesh->getPolygon(polygonIndex);

    auto edgeIt = polygon.getFirstEdge();
    while (edgeIt != polygon.getEndEdge()) {
      auto const& polyEdge = *edgeIt;
      if (edgeFilterFn(mesh, polyEdge.index)) {
        Vector2 v0 = mesh->getVertex(polyEdge.v0).getPosition();
        Vector2 v1 = mesh->getVertex(polyEdge.v1).getPosition();
        addStaticLine(v0, v1, false);
      }

      ++edgeIt;
    }

    // Next polygon
    polygonIndex = mesh->getNextEdgeIndex(polygonIndex);
  }
}

pair<uint32_t, uint32_t> Simulation::addStaticLine(Vector2 const& v0, Vector2 const& v1, bool doubleSided) {
  return addStaticLine(v0.x, v0.y, v1.x, v1.y, doubleSided);
}

StaticLine const& Simulation::getStaticLine(uint32_t index) const {
  return mStaticLines[index];
}

void Simulation::clearStaticLines() {
  mStaticLines.clear();
  if (mStaticLinesGrid) {
    mStaticLinesGrid->clear();
  }
}

AccelerationGrid const* Simulation::getStaticLinesGrid() const {
  return mStaticLinesGrid;
}

AccelerationGrid const* Simulation::getCollidersGrid() const {
  return mCollidersGrid;
}

uint32_t Simulation::getNumStaticLines() const {
  return (uint32_t)mStaticLines.size();
}

void Simulation::enableStaticLines(uint32_t offset, uint32_t count, bool enabled) {
  for (uint32_t i = offset; i < offset + count; ++i) {
    mStaticLines[i].enable(enabled);
  }
}

void Simulation::update(float frameTime) {
  mNumSweepChecks = 0;

  for (auto collider : mColliders) {
    sweepCollider(collider, collider->getMovement() * frameTime);
  }
}

bool Simulation::colliderIntersects(Collider const* collider) const {
  auto lineIndices = getLineIndices(collider->getBounds());

  for (auto const& lineIndex : lineIndices) {
    Vector2 v0, v1;

    auto const& line = mStaticLines[lineIndex];

    if (!line.enabled()) {
      continue;
    }

    SweepResult result;

    if (!collider->fireHitLineCallback(&result, line, 0.0f, mwUserObject)) {
      continue;
    }

    line.getVertices(v0, v1);

    if (collider->intersectsLine(v0, v1)) {
      return true;
    }
  }

  return false;
}

set<uint32_t> Simulation::getLineIndices(wp::BoundingBox const& bounds) const {
  return mStaticLinesGrid->getCandidateItemsInBoundingArea(bounds);
}

bool Simulation::projectCollider(Collider const* collider, Vector2 const& desiredMovement, SweepResult* result) {
  Vector2 colliderCentre = collider->getCentre();
  Vector2 desiredPosition = colliderCentre + desiredMovement;
  Vector2 desiredDirection = desiredMovement.normalisedCopy();

  // Check acceleration grid
  auto const& colliderBounds = collider->getBounds();
  auto movementBounds = colliderBounds.unionWith(BoundingBox(desiredPosition, colliderBounds.getSize()));
  auto lineIndices = getLineIndices(movementBounds);

  // Get all the lines we cross and order them by distance.  Then
  // check each one until we find one which blocks us.
  vector<pair<uint32_t, float>> hitLines;

  for (uint32_t lineIndex : lineIndices) {
    auto const& line = mStaticLines[lineIndex];

    if (!line.enabled()) {
      continue;
    }

    Vector2 v0, v1, lineNormal;

    line.getVertices(v0, v1);
    lineNormal = line.getNormal();

    float angleToLine = desiredDirection.minimumAngleTo(lineNormal, nullptr);
    auto sideOfLine = MathsUtils::pointSideOnLine(colliderCentre, v0, v1);
    bool sideCheck = sideOfLine != MathsUtils::Side::Right && angleToLine > 90;

    if (line.isDoubleSided()) {
      sideCheck |= (sideOfLine != MathsUtils::Side::Left && angleToLine < 90);
    }

    if (sideCheck) {
      float t;
      bool hit;

      hit = collider->sweepAgainstLine(desiredPosition, v0, v1, &t);

      if (hit) {
        hitLines.push_back(make_pair(lineIndex, t));
      }

      mNumSweepChecks++;
    }
  }

  // Closer lines first
  sort(hitLines.begin(), hitLines.end(), [](auto const& x, auto const& y) { return x.second < y.second; });

  result->movementDesired = desiredMovement;
  result->movementDone = desiredMovement;
  result->movementLeft = Vector2::ZERO;
  result->oldPosition = colliderCentre;
  result->newPosition = colliderCentre + result->movementDone;
  result->timeTaken = 1.0f;
  result->distanceMoved = result->movementDone.length();

  for (auto const& hitLine : hitLines) {
    auto const& [lineIndex, lineTime] = hitLine;

    if (collider->fireHitLineCallback(result, mStaticLines[lineIndex], lineTime, mwUserObject)) {
      result->timeTaken = lineTime;
      return true;
    }
  }

  return false;
}

bool Simulation::projectLine(Vector2 const& v0, Vector2 const& v1, SweepResult* result) {
  float closestTime = 1.0f;
  int32_t closestLineIndex = -1;

  // Check acceleration grid
  BoundingBox lineBounds;
  lineBounds.setPosition({std::min(v0.x, v1.x), std::min(v0.y, v1.y)});
  lineBounds.setSize({std::abs(v1.x - v0.x), std::abs(v1.y - v0.y)});

  auto lineIndices = getLineIndices(lineBounds);

  bool hitLine = false;
  for (uint32_t lineIndex : lineIndices) {
    auto const& line = mStaticLines[lineIndex];

    if (!line.enabled()) {
      continue;
    }

    LineHit hit;
    if (MathsUtils::lineLineIntersection(v0, v1, line.getVertex(0), line.getVertex(1), &hit) != MathsUtils::LineIntersectionType::NotIntersecting) {
      hitLine = true;

      // If hit, then move 't' back a small amount to ensure that we are not touching the line.  Otherwise, this will
      // cause floating point accuracy issues
      float t = hit.getTime();

      t -= MathsUtils::Epsilon;

      if (t < closestTime) {
        closestTime = t;
        closestLineIndex = lineIndex;
      }
    }
  }

  result->oldPosition = v0;
  result->newPosition = v0 + (v1 - v0) * closestTime;
  result->timeTaken = closestTime;
  result->movementDesired = v1 - v0;
  result->movementDone = result->newPosition - v0;
  result->movementLeft = v1 - result->newPosition;
  result->distanceMoved = result->movementDone.length();

  return hitLine;
}

void Simulation::sweepCollider(Collider* collider, Vector2 const& desiredMovement, uint32_t sweepCount) {
  constexpr uint32_t MaxSweepCount = 8;
  if (desiredMovement == Vector2::ZERO || sweepCount >= MaxSweepCount) {
    return;
  }

  Vector2 startPosition = collider->getCentre();

  SweepResult result;
  bool continueSweeping = projectCollider(collider, desiredMovement, &result);

  // Update collider position
  collider->_setPosition(result.newPosition);

  // Unfortunately we can go too far and intersect the geometry, so if this happens,
  // reset to start position and bail out.
  if (colliderIntersects(collider)) {
    collider->_setPosition(startPosition);
    return;
  }

  if (collider->isMovementConsumed()) {
    collider->_consumeMovement(result.newPosition - startPosition);
  }

  float remainingDistance = desiredMovement.length() - result.distanceMoved;
  if (remainingDistance > 0 && continueSweeping) {
    sweepCollider(collider, result.movementLeft, sweepCount + 1);
  }
}

int Simulation::getNumSweepChecks() const {
  return mNumSweepChecks;
}

}  // namespace collide
}  // namespace WP_NAMESPACE
