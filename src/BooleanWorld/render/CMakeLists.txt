# Render-geometry stack shared between the BooleanWorld app (the DLL
# Launcher.exe loads) and editor's 3D preview, so both link the exact same
# code instead of two independently drifting copies.

add_library(BooleanWorldRender STATIC
    src/EmbossingCatalog.cpp
    src/EmbossingCatalogResourceDefinitionFactory.cpp
    src/ProcMaterial.cpp
    src/ProcMaterialResourceDefinitionFactory.cpp
    src/SubMaterialResolver.cpp
    src/SurfaceMaterialResolver.cpp
    src/TriplanarMaterial.cpp
    src/TriplanarMaterialResourceDefinitionFactory.cpp
    src/WorldBatch.cpp
    src/WorldBatchRenderer.cpp
    src/WorldRenderer.cpp
    src/WorldRenderer3d.cpp
    src/WorldTriangle3dDataProvider.cpp)

target_include_directories(BooleanWorldRender PUBLIC include)
bw_add_header_filter(BooleanWorldRender)

target_compile_definitions(BooleanWorldRender PRIVATE
    _SILENCE_CXX23_DENORM_DEPRECATION_WARNING)

target_link_libraries(BooleanWorldRender PUBLIC
    core Willpower.Application Willpower.Common
    ext::mpp ext::mpp-helper ext::mpp-mesh ext::Utils)

target_compile_features(BooleanWorldRender PRIVATE cxx_std_23)
target_compile_options(BooleanWorldRender PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
bw_target_defaults(BooleanWorldRender)
bw_output_dirs(BooleanWorldRender)
set_target_properties(BooleanWorldRender PROPERTIES FOLDER BooleanWorld)

willpower_declare_resource_schema(
    RESOURCE_TYPE ProcMaterial
    SCHEMA_ID
        "https://ajare.github.io/boolean-world/schemas/resource-manifest/v1/proc-material.schema.json"
    SOURCE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/schemas/proc-material.schema.json")
willpower_declare_resource_schema(
    RESOURCE_TYPE EmbossingCatalog
    SCHEMA_ID
        "https://ajare.github.io/boolean-world/schemas/resource-manifest/v1/embossing-catalog.schema.json"
    SOURCE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/schemas/embossing-catalog.schema.json")
willpower_declare_resource_schema(
    RESOURCE_TYPE TriplanarMaterial
    SCHEMA_ID
        "https://ajare.github.io/boolean-world/schemas/resource-manifest/v1/triplanar-material.schema.json"
    SOURCE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/schemas/triplanar-material.schema.json")

if(BUILD_TESTING)
    add_executable(boolean_world_wall_normal_map_render_data_tests
        tests/wall_normal_map_render_data_tests.cpp)
    target_link_libraries(boolean_world_wall_normal_map_render_data_tests PRIVATE
        BooleanWorldRender)
    target_compile_definitions(boolean_world_wall_normal_map_render_data_tests PRIVATE
        BW_WORLD_PBR_SHADER="${CMAKE_CURRENT_SOURCE_DIR}/../app/resources/shaders/world_pbr.frag"
        BW_WORLD_PBR_2D_SHADER="${CMAKE_CURRENT_SOURCE_DIR}/../app/resources/shaders/world_pbr_2d.frag")
    target_compile_features(boolean_world_wall_normal_map_render_data_tests PRIVATE cxx_std_23)
    target_compile_options(boolean_world_wall_normal_map_render_data_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
    bw_target_defaults(boolean_world_wall_normal_map_render_data_tests)
    bw_output_dirs(boolean_world_wall_normal_map_render_data_tests)
    bw_deploy_runtime_dlls(boolean_world_wall_normal_map_render_data_tests)
    add_test(NAME boolean_world_wall_normal_map_render_data
             COMMAND boolean_world_wall_normal_map_render_data_tests)
    set_target_properties(boolean_world_wall_normal_map_render_data_tests PROPERTIES
        FOLDER BooleanWorld)

    add_executable(boolean_world_wall_mask_render_data_tests
        tests/wall_mask_render_data_tests.cpp)
    target_link_libraries(boolean_world_wall_mask_render_data_tests PRIVATE
        BooleanWorldRender)
    target_compile_definitions(boolean_world_wall_mask_render_data_tests PRIVATE
        BW_WORLD_PBR_SHADER="${CMAKE_CURRENT_SOURCE_DIR}/../app/resources/shaders/world_pbr.frag"
        BW_WORLD_PBR_2D_SHADER="${CMAKE_CURRENT_SOURCE_DIR}/../app/resources/shaders/world_pbr_2d.frag")
    target_compile_features(boolean_world_wall_mask_render_data_tests PRIVATE cxx_std_23)
    target_compile_options(boolean_world_wall_mask_render_data_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
    bw_target_defaults(boolean_world_wall_mask_render_data_tests)
    bw_output_dirs(boolean_world_wall_mask_render_data_tests)
    bw_deploy_runtime_dlls(boolean_world_wall_mask_render_data_tests)
    add_test(NAME boolean_world_wall_mask_render_data
             COMMAND boolean_world_wall_mask_render_data_tests)
    set_target_properties(boolean_world_wall_mask_render_data_tests PROPERTIES
        FOLDER BooleanWorld)

    add_executable(boolean_world_triplanar_material_load_tests
        tests/triplanar_material_load_tests.cpp)
    target_link_libraries(boolean_world_triplanar_material_load_tests PRIVATE
        BooleanWorldRender)
    target_compile_definitions(boolean_world_triplanar_material_load_tests PRIVATE
        BW_TRIPLANAR_TEST_IMAGE="${CMAKE_CURRENT_SOURCE_DIR}/../app/resources/images/mask1.png")
    target_compile_features(boolean_world_triplanar_material_load_tests PRIVATE cxx_std_23)
    target_compile_options(boolean_world_triplanar_material_load_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
    bw_target_defaults(boolean_world_triplanar_material_load_tests)
    bw_output_dirs(boolean_world_triplanar_material_load_tests)
    bw_deploy_runtime_dlls(boolean_world_triplanar_material_load_tests)
    bw_deploy_vendor_dlls(boolean_world_triplanar_material_load_tests)
    foreach(scenario IN ITEMS
            defaults explicit invalid-tile-low invalid-tile-high invalid-tile-nan
            invalid-sharpness-low invalid-sharpness-high invalid-sharpness-infinite
            missing-image wrong-type non-repeat)
        add_test(NAME boolean_world_triplanar_material_load_${scenario}
                 COMMAND boolean_world_triplanar_material_load_tests ${scenario})
    endforeach()
    set_target_properties(boolean_world_triplanar_material_load_tests PROPERTIES
        FOLDER BooleanWorld)
endif()
