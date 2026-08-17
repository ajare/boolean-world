#pragma once

#include <cstddef>
#include <deque>

namespace bw::common {

template <typename T>
void trimDequeToCapacity(std::deque<T>& values, std::size_t capacity) {
  while (values.size() > capacity) {
    values.pop_front();
  }
}

}  // namespace bw::common
