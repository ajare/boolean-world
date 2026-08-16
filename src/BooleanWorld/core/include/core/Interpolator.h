#pragma once

#define NOMINMAX

#include <vector>
#include <array>
#include <cstdint>
#include <exception>
#include <algorithm>
#include <cassert>
#include <limits>

#include <willpower/common/Globals.h>
#include <willpower/common/BezierSpline.h>

#include "core/Platform.h"
#include "core/Easing.h"
#include "core/Serializable.h"
#include "core/SerializationException.h"
#include "core/CoreException.h"

namespace bw {
namespace core {

template <typename T>
class Interpolator : public Serializable {
public:
  typedef std::pair<float, T> Point;

  static const uint32_t MaxPoints = 16;

public:
  struct Segment {
    Easing easing;
  };

  struct Structure {
    std::vector<Point> points;

    std::vector<Segment> segments;
  };

protected:
  bool mDeltaY;

  Structure mDefaultStructure;

  Structure mCurStructure;

  wp::Vector2 mScale[2];

private:
  bool childrenModified() const override {
    return false;
  }

  void _setPointClamped(Structure& structure, uint32_t index, float time, float value) {
    time = std::clamp(time, mScale[0].x, mScale[1].x);
    value = std::clamp(value, mScale[0].y, mScale[1].y);

    structure.points[index] = {time, value};
  }

  T getValueInSegment(uint32_t index, float baseY, float base2, float time) const {
    assert(index < getNumSegments() && "Interpolator::getValueInSegment(index, segment, baseY, base2, time) - index out of bounds");

    auto const& seg = mCurStructure.segments[index];

    auto p0 = mCurStructure.points[index];
    auto p1 = mCurStructure.points[index + 1];

    p0.second += baseY;
    p1.second += baseY;
    p1.second += base2;

    if (p1.first == p0.first) {
      return p1.second;
    }

    // Convert to unit, ease, then convert back to scale
    time -= p0.first;
    time /= (p1.first - p0.first);

    float dt = ease(seg.easing, time);
    return p0.second + (p1.second - p0.second) * dt;
  }

protected:
  void copyFrom(Interpolator const& other) {
    mDeltaY = other.mDeltaY;
    mDefaultStructure = other.mDefaultStructure;
    mCurStructure = other.mCurStructure;
    mScale[0] = other.mScale[0];
    mScale[1] = other.mScale[1];
  }

  void serializeStructure(Structure const& structure, std::string const& name, std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
    BW_UNUSED(workData);

    serializer->beginMap(name);
    {
      serializer->beginArray("points");
      {
        uint32_t numPoints = (uint32_t)structure.points.size();
        for (uint32_t i = 0; i < numPoints; ++i) {
          auto const& point = structure.points[i];

          serializer->beginMap("point");
          {
            serializer->writeFloat("time", point.first);
            serializer->writeFloat("value", point.second);

            serializer->endMap();
          }
        }

        serializer->endArray();
      }

      serializer->beginArray("segments");
      {
        uint32_t numSegments = (uint32_t)structure.segments.size();
        for (uint32_t i = 0; i < numSegments; ++i) {
          auto const& segment = structure.segments[i];

          serializer->beginMap("segment");
          {
            serializer->writeUint32("easing", (uint32_t)segment.easing);

            serializer->endMap();
          }
        }

        serializer->endArray();
      }

      serializer->endMap();
    }
  }

  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override {
    BW_UNUSED(workData);

    serializer->writeBool("deltaY", mDeltaY);
    serializer->writeVector2("scaleMin", mScale[0]);
    serializer->writeVector2("scaleMax", mScale[1]);
    serializeStructure(mDefaultStructure, "defaultStructure", serializer, workData);
    serializeStructure(mCurStructure, "curStructure", serializer, workData);
  }

  Structure deserializeStructure(std::string const& name, std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
    BW_UNUSED(workData);

    std::vector<Point> points;
    std::vector<Segment> segments;

    serializer->beginMap(name);
    {
      serializer->beginArray("points");
      {
        while (serializer->nextArrayItem()) {
          serializer->beginMap("point");
          {
            float time = serializer->readFloat("time");
            T value = (T)serializer->readFloat("value");

            points.push_back({time, value});

            serializer->endMap();
          }
        }

        serializer->endArray();
      }

      if (points.size() > MaxPoints) {
        throw SerializationException(std::format("More than {} points found in Interpolator.", MaxPoints));
      }

      serializer->beginArray("segments");
      {
        while (serializer->nextArrayItem()) {
          Segment segment;

          serializer->beginMap("segment");
          {
            segment.easing = (Easing)serializer->readUint32("easing");

            serializer->endMap();
          }

          segments.push_back(segment);
        }

        serializer->endArray();
      }

      if ((int)segments.size() != (int)(points.size() - 1)) {
        throw SerializationException("Number of segments in Interpolator does not match number of points");
      }

      serializer->endMap();
    }

    return {
        points,
        segments};
  }

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override {
    BW_UNUSED(workData);

    bool deltaY;
    wp::Vector2 scale[2];
    Structure defaultStructure, curStructure;

    try {
      deltaY = serializer->readBool("deltaY");
      scale[0] = serializer->readVector2("scaleMin");
      scale[1] = serializer->readVector2("scaleMax");
      defaultStructure = deserializeStructure("defaultStructure", serializer, workData);
      curStructure = deserializeStructure("curStructure", serializer, workData);
    } catch (std::exception& e) {
      addDeserializationError(e.what());
      return false;
    }

    // Commit
    mDeltaY = deltaY;
    mScale[0] = scale[0];
    mScale[1] = scale[1];
    mDefaultStructure = defaultStructure;
    mCurStructure = curStructure;
    return true;
  }

public:
  Interpolator()
      : mDeltaY(false), mScale{{0, 0}, {1, 1}} {
  }

  explicit Interpolator(T value)
      : mDeltaY(false), mScale{{0, 0}, {1, 1}} {
    setPoints({{0.0f, value}, {1.0f, value}});
  }

  explicit Interpolator(std::vector<Point> const& points)
      : mDeltaY(false), mScale{{0, 0}, {1, 1}} {
    setPoints(points);
  }

  Interpolator(Interpolator const& other) {
    copyFrom(other);
  }

  Interpolator& operator=(Interpolator const& other) {
    copyFrom(other);
    return *this;
  }

  virtual ~Interpolator() = default;

  void setDefaultStructure(std::vector<Point> const& points, std::vector<Segment> const& segments, bool setToCurrent) {
    mDefaultStructure.points.resize(points.size());

    for (uint32_t i = 0; i < points.size(); i++) {
      _setPointClamped(mDefaultStructure, i, points[i].first, points[i].second);
    }

    mDefaultStructure.segments = segments;

    if (setToCurrent) {
      reset();
    }
  }

  void reset() {
    mCurStructure = mDefaultStructure;
  }

  bool isStatic() const {
    auto numPoints = (uint32_t)mCurStructure.points.size();

    if (numPoints < 2) {
      return true;
    }

    if (mDeltaY) {
      for (uint32_t i = 1; i < numPoints; ++i) {
        if (mCurStructure.points[i].second != 0.0f) {
          return false;
        }
      }

      return true;
    } else {
      auto p = mCurStructure.points[0].second;
      for (uint32_t i = 1; i < numPoints; ++i) {
        if (p != mCurStructure.points[i].second) {
          return false;
        }
      }

      return true;
    }
  }

  void setDeltaY(bool deltaY) {
    mDeltaY = deltaY;
  }

  T getValue(float time) const {
    auto const& points = mCurStructure.points;

    assert(!points.empty() && "Interpolator::getValue(time) - points list is empty");

    if (time <= points.front().first) {
      return points.front().second;
    }

    if (time >= points.back().first) {
      return points.back().second;
    }

    auto numPoints = (int)points.size();
    float baseY{0.0f}, res{std::numeric_limits<float>::quiet_NaN()};

    if (numPoints == 1) {
      res = mCurStructure.points[0].second;
    } else {
      for (int i = 0; i < numPoints - 1; ++i) {
        auto const& p0 = points[i];
        auto const& p1 = points[i + 1];
        if (p0.first == p1.first) {
          if (time == p0.first) {
            res = mDeltaY
                      ? getValueInSegment(i, baseY, p0.second, time)
                      : getValueInSegment(i, 0.0f, 0.0f, time);
            break;
          }
        } else if (p0.first <= time && time < p1.first) {
          res = mDeltaY
                    ? getValueInSegment(i, baseY, p0.second, time)
                    : getValueInSegment(i, 0.0f, 0.0f, time);

          break;
        }

        baseY += p0.second;
      }

      if (std::isnan(res)) {
        auto const& point = points.back();
        res = point.second;
      }
    }

    return res;
  }

  std::vector<std::vector<Point>> render(float resolution) const {
    // Render each segment individually.  Where there are discontinuities,
    // create separate vectors

    std::vector<std::vector<Point>> points;
    std::vector<Point> p;

    auto numSegments = (uint32_t)mCurStructure.segments.size();
    for (uint32_t i = 0; i < numSegments; ++i) {
      auto const& p0 = mCurStructure.points[i + 0];
      auto const& p1 = mCurStructure.points[i + 1];

      float dx = (p1.first - p0.first) / (mScale[1].x - mScale[0].x);

      if (dx == 0.0f) {
        if (!p.empty()) {
          points.push_back(p);
          p.clear();
        }

        continue;
      }

      wp::Vector2 v0 = {p0.first, p0.second};
      wp::Vector2 v1 = {p1.first, p1.second};

      v0 -= mScale[0];
      v1 -= mScale[0];

      v0 /= (mScale[1] - mScale[0]);
      v1 /= (mScale[1] - mScale[0]);

      auto np = (uint32_t)(resolution * dx);
      for (uint32_t j = 0; j < np; ++j) {
        float t = j / (float)(np - 1);
        float dt = ease(mCurStructure.segments[i].easing, t);

        auto x = v0.x + (v1.x - v0.x) * t;
        auto y = v0.y + (v1.y - v0.y) * dt;
        p.push_back({x, y});
      }
    }

    if (!p.empty()) {
      points.push_back(p);
    }

    return points;
  }

  void setScale(wp::Vector2 const& scaleMin, wp::Vector2 const& scaleMax) {
    if (scaleMin.x < 0.0f) {
      throw CoreException("Interpolator time scale cannot go below zero");
    }

    if (scaleMin.x >= scaleMax.x) {
      throw CoreException("Interpolator time scale cannot run backwards");
    }

    mScale[0] = scaleMin;
    mScale[1] = scaleMax;
  }

  void getScale(wp::Vector2* scaleMin, wp::Vector2* scaleMax) const {
    *scaleMin = mScale[0];
    *scaleMax = mScale[1];
  }

  void setPoints(std::vector<std::pair<float, T>> const& points) {
    auto numPoints = (uint32_t)points.size();

    if (numPoints < 2) {
      throw CoreException("An interpolator needs at least two points");
    }

    if (numPoints > MaxPoints) {
      throw std::exception("Too many points");
    }

    mCurStructure.points.resize(numPoints);
    for (uint32_t i = 0; i < numPoints; ++i) {
      if (i > 0 && points[i].first < points[i - 1].first) {
        throw std::exception("Interpolator points not ascending in time");
      }

      _setPointClamped(mCurStructure, i, points[i].first, points[i].second);
    }

    mCurStructure.segments.clear();
    for (uint32_t i = 0; i < numPoints - 1; ++i) {
      mCurStructure.segments.push_back({Easing::Linear});
    }
  }

  void getValueExtents(T* minValue, T* maxValue) const {
    T minV = 1e10f;
    T maxV = -1e10f;

    for (auto const& point : mCurStructure.points) {
      if (minV > point.second) {
        minV = point.second;
      }
      if (maxV < point.second) {
        maxV = point.second;
      }
    }

    *minValue = minV;
    *maxValue = maxV;
  }

  uint32_t getNumPoints() const {
    return (uint32_t)mCurStructure.points.size();
  }

  uint32_t getNumSegments() const {
    auto numPoints = getNumPoints();
    return numPoints > 0 ? numPoints - 1 : 0;
  }

  std::vector<Point> const& getPoints() const {
    return mCurStructure.points;
  }

  void updatePoint(uint32_t index, float time, T const& value) {
    _setPointClamped(mCurStructure, index, time, value);
  }

  void addPoint(float time, T value) {
    auto numPoints = (uint32_t)mCurStructure.points.size();

    if (numPoints == 0) {
      throw std::exception("Cannot add a single point to an empty interpolator");
    } else if (numPoints < MaxPoints) {
      mCurStructure.points.push_back({});
      mCurStructure.segments.push_back({});

      uint32_t i = 0;
      for (; i < numPoints; ++i) {
        auto const& point = mCurStructure.points[i];

        if (point.first > time) {
          break;
        }
      }

      // Shift points up
      for (int j = (int)numPoints; j > (int)i; --j) {
        mCurStructure.points[j] = mCurStructure.points[j - 1];

        if (j < (int)numPoints) {
          mCurStructure.segments[j] = mCurStructure.segments[j - 1];
        }
      }

      numPoints++;

      _setPointClamped(mCurStructure, i, time, value);
      if (i < mCurStructure.segments.size()) {
        mCurStructure.segments[i].easing = Easing::Linear;
      } else {
        mCurStructure.segments.back().easing = Easing::Linear;
      }
    } else {
      throw std::exception("Too many points");
    }
  }

  void removePoint(uint32_t index) {
    assert(index < getNumPoints() && "Interpolator::removePoint(index) - index out of bounds");

    // Shift points down
    auto numPoints = (uint32_t)mCurStructure.points.size();

    if (numPoints == 2) {
      throw std::exception("Cannot have an interpolator with 1 point.");
    }

    numPoints--;

    for (uint32_t i = index; i < numPoints; ++i) {
      mCurStructure.points[i] = mCurStructure.points[i + 1];

      if (i < (numPoints - 1)) {
        mCurStructure.segments[i] = mCurStructure.segments[i + 1];
      }
    }

    mCurStructure.points.pop_back();
    mCurStructure.segments.pop_back();
  }

  void setEasing(uint32_t index, Easing easing) {
    assert(index < getNumSegments() && "Interpolator::setEasing(index, easing) - index out of bounds");

    mCurStructure.segments[index].easing = easing;
  }

  Easing getEasing(uint32_t index) const {
    assert(index < getNumSegments() && "Interpolator::getEasing(index) - index out of bounds");

    return mCurStructure.segments[index].easing;
  }

  std::vector<Segment> const& getSegments() const {
    return mCurStructure.segments;
  }

  Interpolator<T>::Segment const& getSegment(uint32_t index) const {
    assert(index < getNumSegments() && "Interpolator::getSegment(index) - index out of bounds");

    return mCurStructure.segments[index];
  }
};

}  // namespace core
}  // namespace bw