#pragma once

#include <format>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "core/CoreException.h"

namespace bw {
namespace core {

template <typename T>
class Registry {
public:
  using Creator = std::function<T*()>;

  Registry(std::string typeLabel, std::map<std::string, Creator> creators)
      : mTypeLabel(std::move(typeLabel)),
        mCreators(std::move(creators)) {
  }

  [[nodiscard]] std::vector<std::string> getTypes() const {
    std::vector<std::string> types;
    types.reserve(mCreators.size());
    for (auto const& [type, creator] : mCreators) {
      types.push_back(type);
    }
    return types;
  }

  T* create(std::string const& type) const {
    auto it = mCreators.find(type);
    if (it == mCreators.end()) {
      throw CoreException(std::format("No {} of type '{}' registered", mTypeLabel, type));
    }

    return it->second();
  }

private:
  std::string mTypeLabel;
  std::map<std::string, Creator> mCreators;
};

}  // namespace core
}  // namespace bw
