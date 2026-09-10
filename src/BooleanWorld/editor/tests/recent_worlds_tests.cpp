#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "RecentWorlds.h"

namespace {

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void keepsFiveMostRecentlyOpenedWorldsAndPersistsThem() {
  auto root = std::filesystem::temp_directory_path() /
              "boolean-world-editor-recent-worlds-tests";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  auto storage = root / "recent-worlds.txt";

  std::filesystem::path worlds[6];
  editor::RecentWorlds recent(storage);
  for (int index = 0; index < 6; ++index) {
    worlds[index] = root / ("world-" + std::to_string(index) + ".world.yaml");
    std::ofstream(worlds[index]) << "world: {}\n";
    recent.record(worlds[index]);
  }

  require(recent.paths().size() == editor::RecentWorlds::MaximumEntries,
          "the recent list did not cap itself at five Worlds");
  require(recent.paths().front() == std::filesystem::absolute(worlds[5]).string() &&
              recent.paths().back() == std::filesystem::absolute(worlds[1]).string(),
          "the recent list was not ordered newest first");

  recent.record(worlds[3]);
  require(recent.paths().size() == 5 &&
              recent.paths().front() == std::filesystem::absolute(worlds[3]).string(),
          "reopening a World did not move its unique entry to the front");

  editor::RecentWorlds restored(storage);
  require(restored.paths() == recent.paths(),
          "the recent World list did not survive reloading its preferences");

  std::filesystem::remove_all(root);
}

void missingWorldsAreRemovedInsteadOfOpened() {
  auto root = std::filesystem::temp_directory_path() /
              "boolean-world-editor-missing-recent-world-tests";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  auto storage = root / "recent-worlds.txt";
  auto world = root / "missing.world.yaml";

  editor::RecentWorlds recent(storage);
  recent.record(world);
  bool openerCalled = false;
  auto result = recent.open(world, [&](std::string const&) {
    openerCalled = true;
    return true;
  });

  require(result == editor::RecentWorlds::OpenResult::Missing && !openerCalled,
          "a missing recent World was passed to the document loader");
  require(recent.paths().empty(),
          "a missing recent World remained in the list");
  editor::RecentWorlds restored(storage);
  require(restored.paths().empty(),
          "removing a missing recent World was not persisted");

  std::filesystem::remove_all(root);
}

void successfulRecentOpenMovesTheWorldToTheFront() {
  auto root = std::filesystem::temp_directory_path() /
              "boolean-world-editor-open-recent-world-tests";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  auto first = root / "first.world.yaml";
  auto second = root / "second.world.yaml";
  std::ofstream(first) << "world: {}\n";
  std::ofstream(second) << "world: {}\n";

  editor::RecentWorlds recent;
  recent.record(first);
  recent.record(second);
  auto result = recent.open(first, [](std::string const&) { return true; });
  require(result == editor::RecentWorlds::OpenResult::Opened &&
              recent.paths().front() == std::filesystem::absolute(first).string(),
          "opening a recent World did not promote it to the front");

  std::filesystem::remove_all(root);
}

}  // namespace

int main() {
  try {
    keepsFiveMostRecentlyOpenedWorldsAndPersistsThem();
    missingWorldsAreRemovedInsteadOfOpened();
    successfulRecentOpenMovesTheWorldToTheFront();
    std::cout << "Recent Worlds retain five entries and discard missing files\n";
    return 0;
  } catch (std::exception const& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
