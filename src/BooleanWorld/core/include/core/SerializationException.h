#pragma once

#include <exception>
#include <string>

namespace bw {
namespace core {

class SerializationException : public std::exception {
  std::string mMessage;

public:
  explicit SerializationException(std::string message)
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