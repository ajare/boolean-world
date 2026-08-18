#define NOMINMAX
#include <Windows.h>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <core/VertexTransformer.h>
#include <core/World.h>
#include <core/YamlSerializer.h>

namespace {

constexpr float WorldSize = 1536.0f;
constexpr float OrbitAngle = 37.0f;
constexpr float OrbitDistance = 123.0f;

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool equal(float left, float right) {
  return std::fabs(left - right) < 0.0001f;
}

std::string quote(std::filesystem::path const& path) {
  return "\"" + path.string() + "\"";
}

void generateWorld(
    std::filesystem::path const& python,
    std::filesystem::path const& generator,
    std::filesystem::path const& coreDll,
    std::filesystem::path const& output) {
  auto const buildDirectory = coreDll.parent_path()
                                  .parent_path()
                                  .parent_path()
                                  .parent_path();
  _putenv_s("BOOLEANWORLD_BUILD_DIR", buildDirectory.string().c_str());

  auto command = quote(python) + " " + quote(generator) + " " + quote(output);
  STARTUPINFOA startupInfo{};
  startupInfo.cb = sizeof(startupInfo);
  PROCESS_INFORMATION processInfo{};
  require(CreateProcessA(
              nullptr,
              command.data(),
              nullptr,
              nullptr,
              false,
              0,
              nullptr,
              nullptr,
              &startupInfo,
              &processInfo) != 0,
          "Could not start Python world generation");

  WaitForSingleObject(processInfo.hProcess, INFINITE);
  DWORD exitCode = 1;
  GetExitCodeProcess(processInfo.hProcess, &exitCode);
  CloseHandle(processInfo.hThread);
  CloseHandle(processInfo.hProcess);
  require(exitCode == 0, "Python world generation failed");
}

void inspectGeneratedWorld(std::filesystem::path const& output) {
  auto reader = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::fromFile(output.string()));
  reader->deserialize();

  bw::core::World world;
  bw::core::SerializationWorkData workData{WorldSize / 16.0f};
  require(world.deserialize(reader, workData),
          "Python-generated world did not deserialize");

  auto const& extents = world.getExtents();
  require(equal(extents.getSize().x, WorldSize) &&
              equal(extents.getSize().y, WorldSize),
          "Python-generated world size did not round-trip");
  require(world.getNumPrimitives() == 1,
          "Python-generated world has the wrong number of primitives");

  auto const* primitive = world.getPrimitive(0);
  require(equal(primitive->getAnimationInterpolator(
                             bw::core::VertexTransformer::Key::OrbitAngle)
                    .getValue(0.0f),
                OrbitAngle),
          "Python-generated orbit angle did not round-trip");
  require(equal(primitive->getAnimationInterpolator(
                             bw::core::VertexTransformer::Key::OrbitDistance)
                    .getValue(0.0f),
                OrbitDistance),
          "Python-generated orbit distance did not round-trip");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 5) {
    std::cerr << "usage: python_world_generation_tests <python> <generator> <core-dll> <output>\n";
    return 2;
  }

  auto const output = std::filesystem::path(argv[4]);
  try {
    generateWorld(argv[1], argv[2], argv[3], output);
    inspectGeneratedWorld(output);
    std::filesystem::remove(output);
    std::cout << "Python-generated world values round-trip\n";
    return 0;
  } catch (std::exception const& error) {
    std::filesystem::remove(output);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
