#include "EmitterSourcePolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

void generationReconciliationPreservesStablePlaybackIdentity() {
  using Identity = bw::app::EmitterSourceIdentity;
  using Placement = bw::app::EmitterSourcePlacement;
  std::vector<Identity> active{
      {"direct", std::nullopt},
      {"placed", Placement{3, -2, 64}},
      {"vanished", std::nullopt}};
  std::vector<Identity> committed{
      {"placed", Placement{3, -2, 64}},
      {"direct", std::nullopt},
      // Same GUID at another placement is a new source, not a survivor.
      {"placed", Placement{4, -2, 64}}};

  for (int commit = 0; commit < 20; ++commit) {
    auto reconciliation =
        bw::app::reconcileEmitterSources(active, committed);
    require(
        reconciliation.retained ==
                std::vector<std::pair<std::size_t, std::size_t>>{
                    {0, 1}, {1, 0}} &&
            reconciliation.removed == std::vector<std::size_t>{2} &&
            reconciliation.added == std::vector<std::size_t>{2},
        "generation reconciliation restarted or conflated a stable emitter");
  }
}

void cullingIsTableDrivenByAuthoredRadius() {
  struct Case {
    float distance;
    float radius;
    bool expected;
  };
  std::vector<Case> const cases{
      {0.0f, 10.0f, true},
      {10.0f, 10.0f, true},
      {10.01f, 10.0f, false},
      {0.0f, 0.0f, false},
      {-1.0f, 10.0f, false},
      {NAN, 10.0f, false},
      {1.0f, INFINITY, false},
  };

  for (auto const& test : cases) {
    require(
        bw::app::emitterSourceInRange(test.distance, test.radius) ==
            test.expected,
        "emitter distance culling did not follow the authored radius");
  }
}

void rankingUsesNormalizedRatherThanRawDistance() {
  auto decisions = bw::app::selectEmitterSources(
      {{10, 20.0f, 100.0f, false},  // 20% of its radius
       {20, 9.0f, 10.0f, false},    // 90% of its radius
       {30, 5.0f, 4.0f, false}},    // culled
      1, 0.0f);

  require(
      decisions[0].selectedForReflections &&
          !decisions[1].selectedForReflections &&
          !decisions[2].inRange,
      "reflection ranking used raw distance or admitted a culled source");
}

void rankingIsDeterministicAtEqualDistance() {
  auto decisions = bw::app::selectEmitterSources(
      {{20, 2.0f, 10.0f, false}, {10, 4.0f, 20.0f, false}},
      1, 0.0f);
  require(
      !decisions[0].selectedForReflections &&
          decisions[1].selectedForReflections,
      "equal normalized distances were not ordered by stable identity");
}

void hysteresisProtectsTheIncumbentUntilTheMarginIsBeaten() {
  struct Case {
    float challengerDistance;
    bool incumbentExpected;
  };
  std::vector<Case> const cases{
      {44.0f, true},   // 0.44 is nearer than 0.50, but not by 0.10.
      {40.0f, true},   // The exact margin still belongs to the incumbent.
      {39.0f, false},  // A genuine crossing changes the selected source.
  };

  for (auto const& test : cases) {
    auto decisions = bw::app::selectEmitterSources(
        {{1, 50.0f, 100.0f, true},
         {2, test.challengerDistance, 100.0f, false}},
        1, 0.10f);
    require(
        decisions[0].selectedForReflections == test.incumbentExpected &&
            decisions[1].selectedForReflections != test.incumbentExpected,
        "reflection budget hysteresis changed sides at the wrong margin");
  }
}

void leavingRangeOverridesBudgetHysteresis() {
  auto decisions = bw::app::selectEmitterSources(
      {{1, 101.0f, 100.0f, true}, {2, 90.0f, 100.0f, false}},
      1, 0.10f);
  require(
      !decisions[0].inRange && decisions[1].selectedForReflections,
      "an out-of-range incumbent retained a reflection slot");
}

void reflectionContributionFadesBothWays() {
  require(
      bw::app::advanceEmitterFade(0.0f, true, 0.075f, 0.3f) == 0.25f &&
          bw::app::advanceEmitterFade(1.0f, false, 0.075f, 0.3f) ==
              0.75f &&
          bw::app::advanceEmitterFade(0.9f, true, 1.0f, 0.3f) == 1.0f,
      "reflection contribution did not use a bounded short fade");
}

}  // namespace

int main() {
  generationReconciliationPreservesStablePlaybackIdentity();
  cullingIsTableDrivenByAuthoredRadius();
  rankingUsesNormalizedRatherThanRawDistance();
  rankingIsDeterministicAtEqualDistance();
  hysteresisProtectsTheIncumbentUntilTheMarginIsBeaten();
  leavingRangeOverridesBudgetHysteresis();
  reflectionContributionFadesBothWays();
  std::cout << "Emitter culling, ranking, hysteresis, and fade tests passed\n";
}
