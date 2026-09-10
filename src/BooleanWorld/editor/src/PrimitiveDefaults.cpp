#include "Actions.h"
#include "ProcMaterialLibrary.h"

void editor::setPrimitiveDefaultMaterials(bw::core::Primitive* prim) {
  // The default is the Sub-material that uses Technique 0 (the parameterless
  // "Plain grey"), not whichever Sub-material happens to be first in the
  // catalog's list order.
  auto const* defaultMaterial =
      procMaterialLibrary().findSubMaterialByMaterialIndex(0);
  auto properties = prim->getProperties();
  properties.floorMaterial = bw::core::SurfaceMaterialReference::subMaterial(defaultMaterial ? defaultMaterial->id : "");
  properties.ceilingMaterial = bw::core::SurfaceMaterialReference::subMaterial(defaultMaterial ? defaultMaterial->id : "");
  properties.wallMaterial = bw::core::SurfaceMaterialReference::subMaterial(defaultMaterial ? defaultMaterial->id : "");
  prim->setProperties(properties);
}
