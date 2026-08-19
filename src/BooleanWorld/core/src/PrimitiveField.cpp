#include "core/PrimitiveField.h"

#include <algorithm>
#include <cassert>
#include <format>

#include "core/CoreException.h"
#include "core/Defines.h"
#include "core/Layer.h"

namespace bw {
namespace core {

using namespace std;

PrimitiveField::PrimitiveField() = default;

PrimitiveField::~PrimitiveField() {
  clear();
}

string PrimitiveField::getType() const {
  return "PrimitiveField";
}

LayerBuildStep* PrimitiveField::copy(map<VertexTransformerObject const*, VertexTransformerObject*>& primitiveMap) const {
  auto clone = make_unique<PrimitiveField>();
  clone->copyFrom(*this);

  clone->mPrimitives.reserve(mPrimitives.size());
  for (auto const* primitive : mPrimitives) {
    auto* clonedPrimitive = primitive->copy();
    primitiveMap[primitive] = clonedPrimitive;
    clone->mPrimitives.push_back(clonedPrimitive);
  }

  return clone.release();
}

void PrimitiveField::execute(Layer& layer) const {
  for (auto* primitive : mPrimitives) {
    layer._appendBuiltPrimitive(primitive);
  }
}

bool PrimitiveField::childrenModified() const {
  return any_of(mPrimitives.begin(), mPrimitives.end(), [](auto const* primitive) {
    return primitive->isModified();
  });
}

void PrimitiveField::serializeArgs(shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  serializer->beginArray("primitives");
  {
    for (auto const* primitive : mPrimitives) {
      if (!workData.includeGhostPrimitives &&
          (primitive->getFlags() & BW_PRIMITIVE_GHOST_FLAG) != 0) {
        continue;
      }

      serializer->beginMap("primitive");
      {
        serializer->writeString("type", primitive->getType());

        primitive->serialize(serializer, workData);

        serializer->endMap();  // primitive
      }
    }

    serializer->endArray();  // primitives
  }
}

bool PrimitiveField::deserializeArgs(shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  vector<unique_ptr<Primitive>> primitives;

  serializer->beginArray("primitives");
  {
    while (serializer->nextArrayItem()) {
      serializer->beginMap("primitive");
      {
        auto primitiveType = serializer->readString("type");

        auto primitive = unique_ptr<Primitive>(Primitive::instantiate(primitiveType));

        if (!primitive->deserialize(serializer, workData)) {
          copyErrorsAndWarnings(primitive.get(), true, true);
          return false;
        }

        primitives.push_back(move(primitive));

        serializer->endMap();  // primitive
      }
    }

    serializer->endArray();  // primitives
  }

  clear();

  mPrimitives.reserve(primitives.size());
  for (auto& primitive : primitives) {
    mPrimitives.push_back(primitive.release());
  }

  return true;
}

uint32_t PrimitiveField::addPrimitive(Primitive* primitive) {
  auto index = (uint32_t)mPrimitives.size();

  mPrimitives.push_back(primitive);
  modify();

  return index;
}

void PrimitiveField::removePrimitive(Primitive* primitive) {
  delete releasePrimitive(primitive);
}

Primitive* PrimitiveField::releasePrimitive(Primitive* primitive) {
  auto it = find(mPrimitives.begin(), mPrimitives.end(), primitive);
  if (it == mPrimitives.end()) {
    throw CoreException(format("{} primitive {} not found in this PrimitiveField step",
                               primitive->getType(),
                               primitive->getName()));
  }

  mPrimitives.erase(it);
  modify();

  return primitive;
}

void PrimitiveField::replacePrimitive(Primitive* oldPrimitive, Primitive* newPrimitive) {
  auto it = find(mPrimitives.begin(), mPrimitives.end(), oldPrimitive);
  if (it == mPrimitives.end()) {
    throw CoreException(format("{} primitive {} not found in this PrimitiveField step",
                               oldPrimitive->getType(),
                               oldPrimitive->getName()));
  }

  if (oldPrimitive == newPrimitive) {
    return;
  }

  delete *it;
  *it = newPrimitive;
  modify();
}

bool PrimitiveField::contains(Primitive const* primitive) const {
  return find(mPrimitives.begin(), mPrimitives.end(), primitive) != mPrimitives.end();
}

uint32_t PrimitiveField::getNumPrimitives() const {
  return (uint32_t)mPrimitives.size();
}

Primitive* PrimitiveField::getPrimitive(uint32_t index) const {
  assert(index < getNumPrimitives() && "PrimitiveField::getPrimitive(index) - index out of bounds");

  return mPrimitives[index];
}

vector<Primitive*> const& PrimitiveField::getPrimitives() const {
  return mPrimitives;
}

void PrimitiveField::clear() {
  for (auto* primitive : mPrimitives) {
    delete primitive;
  }

  mPrimitives.clear();
}

}  // namespace core
}  // namespace bw
