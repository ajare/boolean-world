#pragma once

#include <exception>
#include <string>

namespace bw {
namespace core {

class NotImplementedException : public std::exception {
  std::string mMessage;

public:
  explicit NotImplementedException(std::string message)
      : mMessage(message) {
  }

  char const* what() const noexcept override {
    return mMessage.c_str();
  }
};

}  // namespace core
}  // namespace bw