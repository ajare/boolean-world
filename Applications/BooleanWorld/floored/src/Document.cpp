#include <exception>
#include <filesystem>
#include <format>

#pragma warning(push)
#pragma warning(disable : 4307)
#include <spdlog/spdlog.h>
#pragma warning(pop)

#include "core/ArrangementWorldDataGenerator.h"
#include "core/YamlSerializer.h"

#include "Document.h"

extern spdlog::logger* gLogger;

namespace floored {
using namespace std;

Document* Document::msInstance = nullptr;

Document::Document() {
}

Document::~Document() {
}

Document* Document::instance() {
  if (!msInstance) {
    msInstance = new Document();
  }

  return msInstance;
}

bool Document::isActive() const {
  return mWorld != nullptr;
}

shared_ptr<bw::core::World> Document::getWorld() {
  return mWorld;
}

std::vector<bw::core::arr::Contour> const& Document::getContours() const {
  return mContours;
}

bw::core::arr::ArrangementResult const& Document::getArrangement() const {
  return *mArrangement;
}

vector<bw::core::arr::ArrangementTriangle> const&
Document::getFaceTriangles() const {
  return mFaceTriangles;
}

void Document::buildArrangement() {
  if (!mWorld) {
    return;
  }

  auto primitives = mWorld->getPrimitives();

  mContours.clear();
  vector<bw::core::arr::ArrangementPrimitive> arrangementPrimitives;
  arrangementPrimitives.reserve(primitives.size());

  for (auto primitive : primitives) {
    auto contours = bw::core::ConvertPrimitiveToContours(*primitive);
    mContours.reserve(mContours.size() + contours.size());
    mContours.insert(mContours.end(), contours.begin(), contours.end());
    arrangementPrimitives.push_back(
        {move(contours),
         primitive->getOperation(),
         primitive->getFillRule(),
         primitive->getPriority(),
         primitive->getId(),
         primitive->getProperties()});
  }

  mArrangement = bw::core::arr::BuildArrangement(arrangementPrimitives);
  mFaceTriangles = bw::core::arr::BuildArrangementTriangles(*mArrangement);
}

bool Document::openWorld(string const& filepath) {
  auto path = filesystem::path(filepath);
  auto ext = path.extension().string();
  transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  if (ext == ".yaml") {
    auto ser = shared_ptr<bw::core::YamlSerializer>(bw::core::YamlSerializer::fromFile(filepath));

    try {
      ser->deserialize();
    } catch (exception& e) {
      gLogger->error(e.what());
      return false;
    }

    mWorld = make_shared<bw::core::World>(1.0f, -1.0f);

    auto workData = bw::core::SerializationWorkData{512.0f};

    if (mWorld->deserialize(ser, workData)) {
      auto const& warnings = mWorld->getDeserializationWarnings();

      if (!warnings.empty()) {
        for (auto const& warning : warnings) {
          gLogger->warn(warning);
        }
      }

      buildArrangement();
      return true;
    } else {
      auto const& errors = mWorld->getDeserializationErrors();

      if (!errors.empty()) {
        for (auto const& error : errors) {
          gLogger->error(error);
        }
      }

      return false;
    }
  } else {
    return false;
  }
}

}  // namespace floored