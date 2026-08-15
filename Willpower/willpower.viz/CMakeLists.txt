# Target name keeps the original solution's spelling ("WillPower.Viz"), so the
# produced WillPower.Viz.dll matches what the launcher and .cfg files expect.
add_library(WillPower.Viz SHARED
    src/AccelerationGridRenderParams.cpp
    src/AccelerationGridRenderer.cpp
    src/CollisionSimulationRenderParams.cpp
    src/CollisionSimulationRenderer.cpp
    src/DynamicLineRenderParams.cpp
    src/DynamicLineRenderer.cpp
    src/DynamicQuadRenderParams.cpp
    src/DynamicRenderer.cpp
    src/DynamicTriangleRenderParams.cpp
    src/FirepowerMeshRenderParams.cpp
    src/FirepowerMeshRenderer.cpp
    src/GeometryMeshRenderParams.cpp
    src/GeometryMeshRenderer.cpp
    src/RenderParams.cpp
    src/Renderer.cpp
    src/StaticRenderer.cpp)

target_include_directories(WillPower.Viz PUBLIC include)

target_compile_definitions(WillPower.Viz PRIVATE
    WIN32 _WINDOWS _USRDLL
    WP_VIZ_DLL_EXPORT
    $<$<CONFIG:Debug>:WP_USE_ASSERT_TRACE WP_USE_MEMLEAK_TRACKING>)

target_link_libraries(WillPower.Viz
    PUBLIC  Willpower.Common Willpower.Collide Willpower.Firepower
            ext::mpp ext::mpp-mesh ext::mpp-helper ext::mpp-program
    PRIVATE ext::Utils vendor::fmt)

target_compile_features(WillPower.Viz PRIVATE cxx_std_20)
target_compile_options(WillPower.Viz PRIVATE /W4)
bw_target_defaults(WillPower.Viz)
bw_output_dirs(WillPower.Viz "build/vs2026/bin/x64/$<CONFIG>" "build/vs2026/lib/x64/$<CONFIG>")
bw_add_version_resource(WillPower.Viz)
set_target_properties(WillPower.Viz PROPERTIES FOLDER Willpower)
