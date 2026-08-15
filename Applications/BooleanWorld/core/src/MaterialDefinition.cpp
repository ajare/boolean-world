#include <algorithm>
#include <array>
#include <bit>

#include <rapidhash/rapidhash.h>

#include "core/MaterialDefinition.h"
#include "core/Defines.h"

namespace bw {
namespace core {

using namespace std;

uint32_t packRGBA(array<float, 3> const& c) {
  auto toByte = [](float v) -> uint32_t {
    v = clamp(v, 0.0f, 1.0f);
    return static_cast<uint32_t>(v * 255.0f + 0.5f);
  };

  uint32_t r = toByte(c[0]);
  uint32_t g = toByte(c[1]);
  uint32_t b = toByte(c[2]);
  uint32_t a = 255;

  return (a << 24) | (b << 16) | (g << 8) | (r);
}

uint64_t MaterialDefinitionData::hash(uint32_t materialIndex) const {
  array<uint32_t, 12> bits;

  bits[0] = materialIndex;

  for (int i = 0; i < BW_MATERIAL_PARAMS_MAX; ++i) {
    auto p = params[i];

    // Negative zero and zero compare equally but have different bit representations
    if (p == 0.0f) {
      p = 0.0f;
    }

    bits[i + 1] = bit_cast<uint32_t>(p);
  }

  for (int i = 0; i < 3; ++i) {
    auto p = baseColour[i];

    if (p == 0.0f) {
      p = 0.0f;
    }

    bits[i + BW_MATERIAL_PARAMS_MAX + 1] = bit_cast<uint32_t>(p);
  }

  return rapidhash(bits.data(), sizeof(bits));
}

bool MaterialDefinition::childrenModified() const {
  return false;
}

void MaterialDefinition::serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  serializer->beginMap("materialDef");
  {
    // Parameters
    serializer->beginArray("params", false);
    {
      for (auto param : data.params) {
        serializer->writeFloat("", param);
      }

      serializer->endArray();
    }

    // Colour
    serializer->beginArray("baseColour", false);
    {
      for (auto component : data.baseColour) {
        serializer->writeFloat("", component);
      }

      serializer->endArray();
    }

    serializer->endMap();
  }
}

bool MaterialDefinition::deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  array<float, BW_MATERIAL_PARAMS_MAX> params{};
  array<float, 3> baseColour{};

  serializer->beginMap("materialDef");
  {
    // Parameters
    serializer->beginArray("params");
    {
      int i = 0;

      while (serializer->nextArrayItem()) {
        if (i >= BW_MATERIAL_PARAMS_MAX) {
          addDeserializationError("Too many MaterialDefinition parameters.");
          return false;
        }

        params[i++] = serializer->readFloat();
      }

      serializer->endArray();
    }

    // Colour
    serializer->beginArray("baseColour");
    {
      int i = 0;

      while (serializer->nextArrayItem()) {
        if (i >= 3) {
          addDeserializationError("Too many colour components.");
          return false;
        }

        baseColour[i++] = serializer->readFloat();
      }

      serializer->endArray();
    }

    serializer->endMap();
  }

  // Commit
  data.params = params;
  data.baseColour = baseColour;

  // Calculate base colour as a uint32 for rendering
  data.baseColourUint = packRGBA(data.baseColour);

  return true;
}

}  // namespace core
}  // namespace bw