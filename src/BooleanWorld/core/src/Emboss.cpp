#include "core/Emboss.h"

#include <array>
#include <utility>

#include "core/Serializer.h"

namespace bw {
namespace core {

using namespace std;

namespace {

constexpr array<pair<EmbossPattern, char const*>, EmbossPatternCount> patternNames{{
    {EmbossPattern::None, "None"},
    {EmbossPattern::Square, "Square"},
    {EmbossPattern::Hexagon, "Hexagon"},
    {EmbossPattern::RunningBond, "RunningBond"},
    {EmbossPattern::ModularOpus, "ModularOpus"},
    {EmbossPattern::Voronoi, "Voronoi"},
}};

bool inRange(float value, EmbossParameterLimits const& limits) {
  return value >= limits.minimum && value <= limits.maximum;
}

}  // namespace

char const* EmbossPatternName(EmbossPattern pattern) {
  for (auto const& [value, name] : patternNames) {
    if (value == pattern) {
      return name;
    }
  }
  return "None";
}

EmbossPattern EmbossPatternFromName(string const& name) {
  for (auto const& [value, patternName] : patternNames) {
    if (name == patternName) {
      return value;
    }
  }
  return EmbossPattern::None;
}

bool EmbossPatternUsesRunningBond(EmbossPattern pattern) {
  return pattern == EmbossPattern::RunningBond;
}

bool EmbossPatternUsesVoronoiRounding(EmbossPattern pattern) {
  return pattern == EmbossPattern::Voronoi;
}

char const* EmbossRadiusName(EmbossPattern pattern) {
  switch (pattern) {
    case EmbossPattern::Hexagon:
      return "Hexagon radius";
    case EmbossPattern::RunningBond:
      return "Tile length";
    case EmbossPattern::ModularOpus:
      return "Large tile size";
    case EmbossPattern::Voronoi:
      return "Cell size";
    case EmbossPattern::Square:
    case EmbossPattern::None:
      break;
  }
  return "Square radius";
}

// The ranges the game's own floor-pattern controls used while embossing was a
// global render option, kept exactly so material authoring reaches the same
// reliefs that were tuned through them.
EmbossParameterLimits EmbossRadiusLimits() {
  return {1.0f, 32.0f};
}

EmbossParameterLimits EmbossDepthLimits() {
  return {0.0f, 8.0f};
}

EmbossParameterLimits EmbossDepthVariationLimits() {
  return {0.0f, 1.0f};
}

EmbossParameterLimits EmbossRunningBondWidthLimits() {
  return {5.0f, 100.0f};
}

EmbossParameterLimits EmbossRunningBondOffsetLimits() {
  return {0.0f, 100.0f};
}

EmbossParameterLimits EmbossVoronoiRoundingLimits() {
  return {0.0f, 1.0f};
}

bool EmbossIsInRange(EmbossData const& emboss) {
  return inRange(emboss.radius, EmbossRadiusLimits()) &&
         inRange(emboss.depth, EmbossDepthLimits()) &&
         inRange(emboss.depthVariation, EmbossDepthVariationLimits()) &&
         inRange(emboss.runningBondWidth, EmbossRunningBondWidthLimits()) &&
         inRange(emboss.runningBondOffset, EmbossRunningBondOffsetLimits()) &&
         inRange(emboss.voronoiRounding, EmbossVoronoiRoundingLimits());
}

void SerializeEmboss(
    shared_ptr<Serializer> const& serializer, string const& name,
    EmbossData const& emboss) {
  serializer->beginMap(name);
  {
    serializer->writeString("pattern", EmbossPatternName(emboss.pattern));
    serializer->writeFloat("radius", emboss.radius);
    serializer->writeFloat("depth", emboss.depth);
    serializer->writeFloat("depthVariation", emboss.depthVariation);
    serializer->writeFloat("runningBondWidth", emboss.runningBondWidth);
    serializer->writeFloat("runningBondOffset", emboss.runningBondOffset);
    serializer->writeFloat("voronoiRounding", emboss.voronoiRounding);

    serializer->endMap();
  }
}

EmbossData DeserializeEmboss(
    shared_ptr<Serializer> const& serializer, string const& name) {
  EmbossData emboss;

  // A positional format has no per-field marker to miss, so every field a
  // writer emitted must be read back in order - see Serializer::isPositional.
  // Nothing writes this block positionally yet; when something does, it will
  // have written all of it.
  auto optional = !serializer->isPositional();

  serializer->beginMap(name);
  {
    emboss.pattern = EmbossPatternFromName(
        serializer->readString("pattern", optional, "None"));
    emboss.radius = serializer->readFloat("radius", optional, emboss.radius);
    emboss.depth = serializer->readFloat("depth", optional, emboss.depth);
    emboss.depthVariation =
        serializer->readFloat("depthVariation", optional, emboss.depthVariation);
    emboss.runningBondWidth = serializer->readFloat(
        "runningBondWidth", optional, emboss.runningBondWidth);
    emboss.runningBondOffset = serializer->readFloat(
        "runningBondOffset", optional, emboss.runningBondOffset);
    emboss.voronoiRounding = serializer->readFloat(
        "voronoiRounding", optional, emboss.voronoiRounding);

    serializer->endMap();
  }

  return emboss;
}

}  // namespace core
}  // namespace bw
