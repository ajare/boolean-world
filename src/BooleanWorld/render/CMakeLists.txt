# Render-geometry stack shared between the BooleanWorld app (the DLL
# Launcher.exe loads) and editor's 3D preview, so both link the exact same
# code instead of two independently drifting copies.

add_library(BooleanWorldRender STATIC
    src/NormalMapResourceSet.cpp
    src/ProcMaterial.cpp
    src/ProcMaterialResourceDefinitionFactory.cpp
    src/SubMaterialResolver.cpp
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
target_compile_options(BooleanWorldRender PRIVATE /W4)
bw_target_defaults(BooleanWorldRender)
bw_output_dirs(BooleanWorldRender)
set_target_properties(BooleanWorldRender PROPERTIES FOLDER BooleanWorld)

if(BUILD_TESTING)
    # Requires a real OpenGL driver, so this is intentionally a manual smoke
    # test rather than a CTest entry.
    add_executable(boolean_world_normal_map_resource_set_smoke
        tests/normal_map_resource_set_smoke.cpp)
    target_link_libraries(boolean_world_normal_map_resource_set_smoke PRIVATE
        BooleanWorldRender vendor::opengl ext::glew ext::sdl3)
    target_compile_features(boolean_world_normal_map_resource_set_smoke PRIVATE cxx_std_23)
    target_compile_options(boolean_world_normal_map_resource_set_smoke PRIVATE /W4)
    bw_target_defaults(boolean_world_normal_map_resource_set_smoke)
    bw_output_dirs(boolean_world_normal_map_resource_set_smoke)
    bw_deploy_runtime_dlls(boolean_world_normal_map_resource_set_smoke)
    bw_deploy_vendor_dlls(boolean_world_normal_map_resource_set_smoke)
    set_target_properties(boolean_world_normal_map_resource_set_smoke PROPERTIES
        FOLDER BooleanWorld)
endif()
