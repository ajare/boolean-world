#include <iostream>

#include <core/InfluenceEye.h>
#include <core/Primitive.h>

namespace {

template <typename Type>
concept HasInArc = requires(Type const& eye, wp::Vector2 const& position) {
  eye.inArc(position);
};

static_assert(!HasInArc<bw::core::InfluenceEye>,
              "InfluenceEye must not expose its unimplemented inArc query");

}  // namespace

int main() {
  std::cout << "Unreachable primitive grouping APIs are absent\n";
  return 0;
}
