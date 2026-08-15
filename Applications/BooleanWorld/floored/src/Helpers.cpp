#include <filesystem>

#include "Helpers.h"

using namespace std;
namespace fs = std::filesystem;

string makeResourcePath(string const& filename) {
  auto filepath = fs::current_path();

  filepath += fs::path("/../../../resources/");
  filepath += fs::path(filename);

  return filepath.string();
}