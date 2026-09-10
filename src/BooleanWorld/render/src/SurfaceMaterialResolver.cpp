#include "SurfaceMaterialResolver.h"

#include <array>
#include <stdexcept>
#include <utility>

#include <rapidhash/rapidhash.h>

#include <core/Defines.h>

using namespace std;
using namespace wp::application::resourcesystem;

SurfaceMaterialResolver::SurfaceMaterialResolver(
    SubMaterialResolver const& subMaterials, ResourceManager* resourceManager,
    string currentNamespace)
    : mSubMaterials(&subMaterials),
      mResourceManager(resourceManager),
      mCurrentNamespace(move(currentNamespace)) {}

SurfaceMaterialResolver::Resolved SurfaceMaterialResolver::resolve(
    bw::core::SurfaceMaterialReference const& reference,
    string const& embossPresetId) const {
  if (reference.kind == bw::core::SurfaceMaterialKind::SubMaterial) {
    auto procedural = mSubMaterials->resolve(reference.reference, embossPresetId);
    return {
        procedural.materialIndex, procedural.def, nullptr,
        procedural.def.hash(procedural.materialIndex)};
  }

  string namesp;
  string name;
  Resource::splitName(
      reference.reference, mCurrentNamespace, &namesp, &name);
  auto resource = mResourceManager->getResource(name, namesp);
  auto material = dynamic_pointer_cast<TriplanarMaterial>(resource);
  if (!material || !mResourceManager->isResourceLoaded(resource)) {
    throw runtime_error(
        "Triplanar Surface material '" + reference.reference +
        "' is not a loaded TriplanarMaterial resource.");
  }

  Resolved result;
  result.materialIndex = BW_TRIPLANAR_MATERIAL_INDEX;
  result.triplanar = material.get();
  result.def.baseColour = {1.0f, 1.0f, 1.0f};
  result.def.emboss = mSubMaterials->resolve("", embossPresetId).def.emboss;
  result.def.params.fill(0.0f);

  // Keep Resource identity in the render bucket without leaking it into the
  // fixed shader inputs. Embossing remains part of MaterialDefinition's hash,
  // so one Triplanar resource can still own distinct preset-specific buckets.
  array<uint64_t, 2> identity{
      rapidhash(reference.reference.data(), reference.reference.size()),
      result.def.hash(static_cast<uint32_t>(result.materialIndex))};
  result.bucketHash = rapidhash(identity.data(), sizeof(identity));
  return result;
}
