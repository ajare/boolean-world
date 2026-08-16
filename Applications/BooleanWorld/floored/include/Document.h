#pragma once

#include <string>
#include <set>
#include <vector>

#include <willpower/common/Vector2.h>

#include "core/World.h"

#include <core/Arrangement.h>

namespace floored {

class Document {
  std::shared_ptr<bw::core::World> mWorld;

  std::vector<bw::core::arr::Contour> mContours;

  bw::core::arr::ArrangementResultPtr mArrangement;

  std::vector<bw::core::arr::ArrangementTriangle> mFaceTriangles;

  static Document* msInstance;

private:
  void buildArrangement();

public:
  Document();

  virtual ~Document();

  static Document* instance();

  bool isActive() const;

  std::shared_ptr<bw::core::World> getWorld();

  std::vector<bw::core::arr::Contour> const& getContours() const;

  bw::core::arr::ArrangementResult const& getArrangement() const;

  std::vector<bw::core::arr::ArrangementTriangle> const& getFaceTriangles() const;

  bool openWorld(std::string const& filepath);
};

}  // namespace floored
