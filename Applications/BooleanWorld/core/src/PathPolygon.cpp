#include <clipper2/clipper.h>

#include "core/ClipperDefines.h"
#include "core/ClipperUtils.h"
#include "core/PathPolygon.h"
#include "core/NotImplementedException.h"

namespace bw {
namespace core {

using namespace std;

Clipper2Lib::Paths64 inflatePaths(Clipper2Lib::Paths64 const& paths, float width, PathPolygon::JoinType joinType, PathPolygon::EndType endType) {
  Clipper2Lib::JoinType jt;
  Clipper2Lib::EndType et;

  switch (joinType) {
    case PathPolygon::JoinType::Bevel:
      jt = Clipper2Lib::JoinType::Bevel;
      break;

    case PathPolygon::JoinType::Mitre:
      jt = Clipper2Lib::JoinType::Miter;
      break;

    case PathPolygon::JoinType::Round:
      jt = Clipper2Lib::JoinType::Round;
      break;

    case PathPolygon::JoinType::Square:
      jt = Clipper2Lib::JoinType::Square;
      break;

    default:
      jt = Clipper2Lib::JoinType::Bevel;
      break;
  }

  switch (endType) {
    case PathPolygon::EndType::Butt:
      et = Clipper2Lib::EndType::Butt;
      break;

    case PathPolygon::EndType::Joined:
      et = Clipper2Lib::EndType::Joined;
      break;

    case PathPolygon::EndType::Polygon:
      et = Clipper2Lib::EndType::Polygon;
      break;

    case PathPolygon::EndType::Round:
      et = Clipper2Lib::EndType::Round;
      break;

    case PathPolygon::EndType::Square:
      et = Clipper2Lib::EndType::Square;
      break;

    default:
      et = Clipper2Lib::EndType::Butt;
      break;
  }

  return Clipper2Lib::InflatePaths(paths, width * 0.5f, jt, et);
}

PathPolygon::PathPolygon()
    : Primitive(), mPoints(), mWidth(1.0f), mJoinType(JoinType::Square), mEndType(EndType::Square) {
}

PathPolygon::PathPolygon(Operation operation, FillRule fillType, vector<wp::Vector2> const& points, float width, JoinType joinType, EndType endType)
    : Primitive(operation, fillType), mPoints(points), mWidth(width), mJoinType(joinType), mEndType(endType) {
  generateVertices();
}

PathPolygon::PathPolygon(PathPolygon const& other) {
  copyFrom(other);
}

PathPolygon& PathPolygon::operator=(PathPolygon const& other) {
  copyFrom(other);
  return *this;
}

void PathPolygon::copyFrom(PathPolygon const& other) {
  Primitive::copyFrom(other);
}

Primitive* PathPolygon::copy() const {
  return new PathPolygon(*this);
}

string PathPolygon::getType() const {
  return "Path";
}

void PathPolygon::serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  Primitive::serializeImpl(serializer, workData);

  serializer->beginMap("pathPolygon");
  {
    serializer->beginArray("points", false);
    {
      for (auto const& point : mPoints) {
        serializer->writeVector2("point", point);
      }

      serializer->endArray();
    }

    serializer->writeFloat("width", mWidth);
    serializer->writeUint32("joinType", (uint32_t)mJoinType);
    serializer->writeUint32("endType", (uint32_t)mEndType);

    serializer->endMap();  // pathPolygon
  }
}

bool PathPolygon::deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  if (!Primitive::deserializeImpl(serializer, workData)) {
    return false;
  }

  vector<wp::Vector2> points;
  float width;
  JoinType joinType;
  EndType endType;

  try {
    serializer->beginMap("pathPolygon");
    {
      serializer->beginArray("values");
      {
        while (serializer->nextArrayItem()) {
          points.push_back(serializer->readVector2("point"));
        }

        serializer->endArray();
      }

      width = serializer->readFloat("width");
      joinType = (JoinType)serializer->readUint32("joinType");
      endType = (EndType)serializer->readUint32("endType");

      serializer->endMap();  // pathPolygon
    }
  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  // Commit
  mPoints = points;
  mWidth = width;
  mJoinType = joinType;
  mEndType = endType;
  return true;
}

vector<ComplexPolygon> PathPolygon::generateVerticesImpl() {
  Clipper2Lib::Path64 polyline;

  for (auto const& point : mPoints) {
    polyline.push_back(BW_CLIPPER_MAKE_POINT(point.x, point.y, 0));
  }

  auto solution = inflatePaths({polyline}, mWidth, mJoinType, mEndType);

  ClosedPolygon vertices;

  for (auto const& point : solution[0]) {
    auto x = static_cast<float>(point.x) / BW_CLIPPER_SCALE;
    auto y = static_cast<float>(point.y) / BW_CLIPPER_SCALE;

    vertices.push_back({wp::Vector2(x, y)});
  }

  return {{vertices}};
}

float PathPolygon::getRadius() const {
  throw NotImplementedException("PathPolygon::getRadius()");
}

void PathPolygon::setPoints(vector<wp::Vector2> const& points) {
  mPoints = points;
  generateVertices();
}

vector<wp::Vector2> const& PathPolygon::getPoints() const {
  return mPoints;
}

void PathPolygon::setWidth(float width) {
  mWidth = width;
  generateVertices();
}

float PathPolygon::getWidth() const {
  return mWidth;
}

void PathPolygon::setJoinType(JoinType joinType) {
  mJoinType = joinType;
  generateVertices();
}

PathPolygon::JoinType PathPolygon::getJoinType() const {
  return mJoinType;
}

void PathPolygon::setEndType(EndType endType) {
  mEndType = endType;
  generateVertices();
}

PathPolygon::EndType PathPolygon::getEndType() const {
  return mEndType;
}

}  // namespace core
}  // namespace bw