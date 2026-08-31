#pragma once

namespace bw::app {

// Application-run policy for generating WorldData. The launcher transfers
// these values through a dedicated scalar DLL export rather than string args.
struct WorldDataGenerationOptions {
  float startInterval{5.0f};

  bool operator==(WorldDataGenerationOptions const&) const = default;
};

}  // namespace bw::app
