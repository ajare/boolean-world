#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace editor {

class RecentWorlds {
public:
  static constexpr std::size_t MaximumEntries = 5;

  enum class OpenResult {
    Opened,
    Missing,
    Failed
  };

  explicit RecentWorlds(std::filesystem::path storagePath = {});

  [[nodiscard]] std::vector<std::string> const& paths() const;
  void record(std::filesystem::path const& path);
  void remove(std::filesystem::path const& path);
  OpenResult open(
      std::filesystem::path const& path,
      std::function<bool(std::string const&)> const& opener);

private:
  std::filesystem::path mStoragePath;
  std::vector<std::string> mPaths;

  static std::string normalise(std::filesystem::path const& path);
  void load();
  void save() const;
};

}  // namespace editor
