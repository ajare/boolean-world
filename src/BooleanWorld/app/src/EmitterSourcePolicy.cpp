#include "EmitterSourcePolicy.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bw::app {

EmitterSourceReconciliation reconcileEmitterSources(
    std::vector<EmitterSourceIdentity> const& previous,
    std::vector<EmitterSourceIdentity> const& next) {
  EmitterSourceReconciliation result;
  std::vector<bool> nextMatched(next.size(), false);
  for (std::size_t previousIndex = 0;
       previousIndex < previous.size(); ++previousIndex) {
    auto nextIndex = std::size_t{};
    for (; nextIndex < next.size(); ++nextIndex) {
      if (!nextMatched[nextIndex] &&
          previous[previousIndex] == next[nextIndex]) {
        break;
      }
    }
    if (nextIndex < next.size()) {
      nextMatched[nextIndex] = true;
      result.retained.emplace_back(previousIndex, nextIndex);
    } else {
      result.removed.push_back(previousIndex);
    }
  }
  for (std::size_t nextIndex = 0; nextIndex < next.size(); ++nextIndex) {
    if (!nextMatched[nextIndex]) result.added.push_back(nextIndex);
  }
  return result;
}

bool emitterSourceInRange(float distance, float cullRadius) {
  return std::isfinite(distance) && distance >= 0.0f &&
         std::isfinite(cullRadius) && cullRadius > 0.0f &&
         distance <= cullRadius;
}

float normalizedEmitterSourceDistance(float distance, float cullRadius) {
  if (!emitterSourceInRange(distance, cullRadius)) {
    return std::numeric_limits<float>::infinity();
  }
  return distance / cullRadius;
}

std::vector<EmitterSourceDecision> selectEmitterSources(
    std::vector<EmitterSourceCandidate> const& candidates,
    std::size_t reflectionCapacity,
    float hysteresisMargin) {
  struct Ranked {
    std::size_t decisionIndex{};
    float normalizedDistance{};
    bool previouslySelected{};
  };

  std::vector<EmitterSourceDecision> result;
  std::vector<Ranked> ranked;
  result.reserve(candidates.size());
  ranked.reserve(candidates.size());

  for (auto const& candidate : candidates) {
    auto const normalized = normalizedEmitterSourceDistance(
        candidate.distance, candidate.cullRadius);
    auto const inRange = std::isfinite(normalized);
    result.push_back(
        {candidate.key, normalized, inRange, false});
    if (inRange) {
      ranked.push_back(
          {result.size() - 1, normalized,
           candidate.previouslySelectedForReflections});
    }
  }

  auto nearer = [&result](Ranked const& lhs, Ranked const& rhs) {
    if (lhs.normalizedDistance != rhs.normalizedDistance) {
      return lhs.normalizedDistance < rhs.normalizedDistance;
    }
    return result[lhs.decisionIndex].key < result[rhs.decisionIndex].key;
  };
  std::sort(ranked.begin(), ranked.end(), nearer);

  if (reflectionCapacity == 0 || ranked.empty()) return result;
  if (!std::isfinite(hysteresisMargin) || hysteresisMargin < 0.0f) {
    hysteresisMargin = 0.0f;
  }

  std::vector<Ranked> selected;
  selected.reserve(std::min(reflectionCapacity, ranked.size()));

  // Preserve eligible incumbents first. Sorting keeps cap reductions
  // deterministic and retains the nearest incumbents.
  for (auto const& source : ranked) {
    if (source.previouslySelected && selected.size() < reflectionCapacity) {
      selected.push_back(source);
    }
  }

  auto selectedContains = [&selected](std::size_t decisionIndex) {
    return std::ranges::any_of(
        selected, [decisionIndex](Ranked const& source) {
          return source.decisionIndex == decisionIndex;
        });
  };

  for (auto const& challenger : ranked) {
    if (selectedContains(challenger.decisionIndex)) continue;

    if (selected.size() < reflectionCapacity) {
      selected.push_back(challenger);
      continue;
    }

    auto worst = std::max_element(selected.begin(), selected.end(), nearer);
    // A tie, or a movement smaller than the margin, belongs to the incumbent.
    if (challenger.normalizedDistance + hysteresisMargin <
        worst->normalizedDistance) {
      *worst = challenger;
    }
  }

  for (auto const& source : selected) {
    result[source.decisionIndex].selectedForReflections = true;
  }
  return result;
}

float advanceEmitterFade(
    float contribution, bool enabled, float frameTime, float fadeDuration) {
  contribution = std::clamp(contribution, 0.0f, 1.0f);
  auto const target = enabled ? 1.0f : 0.0f;
  if (!std::isfinite(frameTime) || frameTime <= 0.0f) return contribution;
  if (!std::isfinite(fadeDuration) || fadeDuration <= 0.0f) return target;

  auto const step = frameTime / fadeDuration;
  return target > contribution
             ? std::min(target, contribution + step)
             : std::max(target, contribution - step);
}

}  // namespace bw::app
