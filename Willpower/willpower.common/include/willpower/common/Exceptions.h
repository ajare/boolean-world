#pragma once

#include <string>
#include <exception>

#if _MSC_VER >= 1930
#include <source_location>
#endif

#include "utils/StringUtils.h"

#include "willpower/common/Platform.h"

namespace WP_NAMESPACE {

class Exception : public std::exception {
public:
  explicit Exception(std::string const& message)
      : std::exception(message.c_str()) {
  }
};

class NotImplementedException : public Exception {
public:
#if _MSC_VER < 1930
  NotImplementedException()
      : Exception("Not implemented yet.") {
  }
#else
  NotImplementedException(std::source_location loc = std::source_location::current())
      : Exception(STR_FORMAT("Function {} at {}:{} is not implemented yet.", loc.function_name(), loc.file_name(), loc.line())) {
  }
#endif

  explicit NotImplementedException(std::string const& function)
      : Exception(function + " is not implemented yet.") {
  }

  NotImplementedException(std::string const& function, std::string const& msg)
      : Exception(function + ": " + msg + " is not implemented yet.") {
  }
};

}  // namespace WP_NAMESPACE

#define NOT_IMPLEMENTED_YET(msg) throw wp::NotImplementedException(__FUNCTION__, msg)