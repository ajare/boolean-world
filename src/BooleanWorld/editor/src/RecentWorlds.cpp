#include "RecentWorlds.h"

#include <algorithm>
#include <fstream>
#include <iomanip>

namespace editor {
using namespace std;

RecentWorlds::RecentWorlds(filesystem::path storagePath)
    : mStoragePath(move(storagePath)) {
  load();
}

vector<string> const& RecentWorlds::paths() const {
  return mPaths;
}

string RecentWorlds::normalise(filesystem::path const& path) {
  error_code error;
  auto absolute = filesystem::absolute(path, error);
  return (error ? path : absolute).lexically_normal().string();
}

void RecentWorlds::record(filesystem::path const& path) {
  auto value = normalise(path);
  mPaths.erase(std::remove(mPaths.begin(), mPaths.end(), value), mPaths.end());
  mPaths.insert(mPaths.begin(), move(value));
  if (mPaths.size() > MaximumEntries) {
    mPaths.resize(MaximumEntries);
  }
  save();
}

void RecentWorlds::remove(filesystem::path const& path) {
  auto value = normalise(path);
  auto end = std::remove(mPaths.begin(), mPaths.end(), value);
  if (end != mPaths.end()) {
    mPaths.erase(end, mPaths.end());
    save();
  }
}

RecentWorlds::OpenResult RecentWorlds::open(
    filesystem::path const& path,
    function<bool(string const&)> const& opener) {
  auto value = normalise(path);
  error_code error;
  if (!filesystem::is_regular_file(value, error)) {
    remove(value);
    return OpenResult::Missing;
  }
  if (!opener(value)) {
    return OpenResult::Failed;
  }
  record(value);
  return OpenResult::Opened;
}

void RecentWorlds::load() {
  if (mStoragePath.empty()) {
    return;
  }
  ifstream input(mStoragePath);
  string value;
  while (mPaths.size() < MaximumEntries && input >> quoted(value)) {
    if (!value.empty() &&
        find(mPaths.begin(), mPaths.end(), value) == mPaths.end()) {
      mPaths.push_back(move(value));
    }
  }
}

void RecentWorlds::save() const {
  if (mStoragePath.empty()) {
    return;
  }
  error_code error;
  if (!mStoragePath.parent_path().empty()) {
    filesystem::create_directories(mStoragePath.parent_path(), error);
  }
  ofstream output(mStoragePath, ios::trunc);
  for (auto const& path : mPaths) {
    output << quoted(path) << '\n';
  }
}

}  // namespace editor
