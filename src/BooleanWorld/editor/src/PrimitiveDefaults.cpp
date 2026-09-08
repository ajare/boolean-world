#include "Actions.h"
#include "ProcMaterialLibrary.h"

void editor::setPrimitiveDefaultMaterials(bw::core::Primitive* prim) {
  // The default is the Sub-material that uses Technique 0 (the parameterless
  // "Plain grey"), not whichever Sub-material happens to be first in the
  // catalog's list order.
  auto const* defaultMaterial =
      procMaterialLibrary().findSubMaterialByMaterialIndex(0);
  auto properties = prim->getProperties();
  properties.floorMaterialId = defaultMaterial ? defaultMaterial->id : "";
  properties.ceilingMaterialId = defaultMaterial ? defaultMaterial->id : "";
  properties.wallMaterialId = defaultMaterial ? defaultMaterial->id : "";
  prim->setProperties(properties);
}
