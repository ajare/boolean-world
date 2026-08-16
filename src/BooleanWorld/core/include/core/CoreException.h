#pragma once

#include <exception>
#include <string>

namespace bw {
namespace core {

class CoreException : public std::exception {
  std::string mMessage;

public:
  explicit CoreException(std::string message)
      : mMessage(message) {
  }

  char const* what() const noexcept override {
    return mMessage.c_str();
  }

  std::string const& getMessage() const {
    return mMessage;
  }
};

}  // namespace core
}  // namespace bw