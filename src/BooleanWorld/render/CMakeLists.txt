# Render-geometry stack shared between the BooleanWorld app (the DLL
# Launcher.exe loads) and editor's 3D preview, so both link the exact same
# code instead of two independently drifting copies.

add_library(BooleanWorldRender STATIC
    src/ProcMaterial.cpp
    src/ProcMaterialResourceDefinitionFactory.cpp
    src/SubMaterialResolver.cpp
    src/WorldBatch.cpp
    src/WorldBatchRenderer.cpp
    src/WorldRenderer.cpp
    src/WorldRenderer3d.cpp
    src/WorldTriangle3dDataProvider.cpp
    src/WorldWallOrientation.cpp)

target_include_directories(BooleanWorldRender PUBLIC include)
bw_add_header_filter(BooleanWorldRender)

target_compile_definitions(BooleanWorldRender PRIVATE
    _SILENCE_CXX23_DENORM_DEPRECATION_WARNING)

target_link_libraries(BooleanWorldRender PUBLIC
    core Willpower.Application Willpower.Common
    ext::mpp ext::mpp-helper ext::mpp-mesh ext::Utils)

target_compile_features(BooleanWorldRender PRIVATE cxx_std_23)
target_compile_options(BooleanWorldRender PRIVATE /W4)
bw_target_defaults(BooleanWorldRender)
bw_output_dirs(BooleanWorldRender)
set_target_properties(BooleanWorldRender PROPERTIES FOLDER BooleanWorld)
