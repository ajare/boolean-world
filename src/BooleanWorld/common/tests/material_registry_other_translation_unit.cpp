#include <core/Defines.h>

#include <common/MaterialRegistry.h>

#include "material_registry_test_helper.h"

void const* materialNamesFromOtherTranslationUnit() {
  return &bw::common::MaterialNames;
}

void const* materialParamsFromOtherTranslationUnit() {
  return &bw::common::MaterialParams;
}
