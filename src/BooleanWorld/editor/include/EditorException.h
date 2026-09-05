#pragma once

#include <stdexcept>
#include <string>

class EditorException : public std::runtime_error {
  std::string mMessage;

public:
  explicit EditorException(std::string message)
      : std::runtime_error(message), mMessage(message) {
  }

  std::string const& getMessage() const {
    return mMessage;
  }
};
