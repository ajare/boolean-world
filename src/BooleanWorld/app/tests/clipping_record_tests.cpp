#include <iostream>
#include <stdexcept>

#include <common/BoundedDeque.h>

#include "ClippingRecord.h"

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void formatsGenerationTimeBeyond32Bits() {
  ClippingRecord record{};
  record.generationTimeNs = (uint64_t{1} << 32) * 1000;

  require(record.generationTimeUsText() == "4294967296",
          "generation time diagnostic truncated a 64-bit microsecond value");
}

void omitsLagUntilGenerationCompletes() {
  ClippingRecord record{};
  record.commitedTime = 10.0;

  require(!record.commitLag().has_value(),
          "commit lag was reported before generation completed");

  record.generationCompleteTime = 9.25;
  require(record.commitLag() == 0.75,
          "commit lag did not use the completed generation time");
}

void retainsTheConfiguredDebugRingBufferCapacity() {
  std::deque<int> values;
  for (int i = 0; i <= 128; ++i) {
    values.push_back(i);
    bw::common::trimDequeToCapacity(values, 128);
  }
  require(values.size() == 128 && values.front() == 1 && values.back() == 128,
          "display-message ring buffer did not retain its configured capacity");

  values.clear();
  for (int i = 0; i <= 10; ++i) {
    values.push_back(i);
    bw::common::trimDequeToCapacity(values, 10);
  }
  require(values.size() == 10 && values.front() == 1 && values.back() == 10,
          "clipping-record ring buffer did not retain its configured capacity");
}

}  // namespace

int main() {
  try {
    formatsGenerationTimeBeyond32Bits();
    omitsLagUntilGenerationCompletes();
    retainsTheConfiguredDebugRingBufferCapacity();
    std::cout << "Clipping record diagnostics regression passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
