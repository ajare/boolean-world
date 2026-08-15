#pragma once

#include <functional>
#include <cassert>

#include <willpower/common/Vector2.h>

namespace applib {

template <class T>
class Battery {
  typedef std::function<bool(T&, void*, float)> ObjectUpdateFunction;

private:
  std::vector<T> mCreatedObjects;

  std::vector<T> mObjects;

  size_t mCount;

  ObjectUpdateFunction mUpdateFunc;

private:
  void resize(size_t newSize) {
    auto curSize = mObjects.size();
    if (newSize > curSize) {
      mObjects.resize(newSize);
    }
  }

  void autoResize(size_t newSize) {
    if (newSize > mObjects.size()) {
      auto tgtSize = mObjects.size();
      while (tgtSize < newSize) {
        tgtSize *= 3;
        tgtSize /= 2;
        tgtSize += 8;
      }

      resize(tgtSize);
    }
  }

  bool freeCapacity() const {
    return mCount < mObjects.size();
  }

  void addCreatedObjects() {
    // Added created objects
    autoResize(mCount + mCreatedObjects.size());

    for (auto const& object : mCreatedObjects) {
      mObjects[mCount++] = object;
    }

    mCreatedObjects.clear();
  }

public:
  Battery(size_t initialSize, ObjectUpdateFunction updateFunc)
      : mCount(0), mUpdateFunc(updateFunc) {
    resize(initialSize);
  }

  size_t getCount() const {
    return mCount;
  }

  T const& getObject(uint32_t index) const {
    assert(index < mCount && "Index out of bounds!");
    return mObjects[index];
  }

  T& getObject(uint32_t index) {
    assert(index < mCount && "Index out of bounds!");
    return mObjects[index];
  }

  void addObject(T const& object, bool addImmediately = false) {
    if (addImmediately) {
      autoResize(mCount + 1);
      mObjects[mCount++] = object;
    } else {
      mCreatedObjects.push_back(object);
    }
  }

  void update(void* userObj, float frameTime) {
    // Add new objects
    addCreatedObjects();

    // Update objects
    size_t tCount{0};
    for (size_t i = 0; i < mCount; ++i) {
      auto& object = mObjects[i];

      if (mUpdateFunc(object, userObj, frameTime)) {
        if (i != tCount) {
          mObjects[tCount] = mObjects[i];
        }

        tCount++;
      } else {
        // Destroy objects
      }
    }

    mCount = tCount;
  }
};

}  // namespace applib