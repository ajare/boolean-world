#include <iostream>
#include <stdexcept>

#include <common/GameDefines.h>
#include <core/Defines.h>

#include <common/MaterialRegistry.h>

#include "material_registry_test_helper.h"

namespace {

static_assert(BW_WORLD_CEILING_HEIGHT_MAX == 200.0f);

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void materialTablesHaveOneDefinitionAcrossTranslationUnits() {
  require(&bw::common::MaterialNames == materialNamesFromOtherTranslationUnit(),
          "MaterialNames has a separate definition in another translation unit");
  require(&bw::common::MaterialParams == materialParamsFromOtherTranslationUnit(),
          "MaterialParams has a separate definition in another translation unit");
}

void materialTablesPreserveTheirDefinitions() {
  require(std::get<0>(bw::common::MaterialNames[0]) == "Marble",
          "the Marble material definition changed");
  require(std::get<3>(bw::common::MaterialParams[0][0]) == 1.1f,
          "the Marble warp scale default changed");
}

}  // namespace

int main() {
  try {
    materialTablesHaveOneDefinitionAcrossTranslationUnits();
    materialTablesPreserveTheirDefinitions();
    std::cout << "Material registry coverage passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
