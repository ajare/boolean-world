# The game itself: a DLL that Launcher.exe loads at runtime.

add_library(BooleanWorld SHARED
    src/DLL.cpp
    src/EntityHandlerBooleanWorld.cpp
    src/Game.cpp
    src/GameDefinitionFactory.cpp
    src/Map.cpp
    src/MapBooleanWorldDefinitionFactory.cpp
    src/ProtoEntity.cpp
    src/ProtoEntityDefinitionFactory.cpp
    src/ReactiveCamera.cpp
    src/StateControllerBooleanWorld.cpp
    src/StateMapLoadBooleanWorld.cpp
    src/StateMapTransitionBooleanWorld.cpp
    src/StateMapUnloadBooleanWorld.cpp
    src/StatePlayBooleanWorld.cpp
    src/WorldBatch.cpp
    src/WorldBatchRenderer.cpp
    src/WorldCollisionSim.cpp
    src/WorldRenderer.cpp
    src/WorldRenderer3d.cpp
    src/WorldTriangle3dDataProvider.cpp
    # Vendored ImGui + ImPlot. Each BooleanWorld app carries its own copy;
    # this one and Launcher's are a newer ImGui than editor's and floored's.
    src/imgui/imgui.cpp
    src/imgui/imgui_demo.cpp
    src/imgui/imgui_draw.cpp
    src/imgui/imgui_tables.cpp
    src/imgui/imgui_widgets.cpp
    src/imgui/implot.cpp
    src/imgui/implot_demo.cpp
    src/imgui/implot_items.cpp)

target_include_directories(BooleanWorld PRIVATE include include/imgui)

target_compile_definitions(BooleanWorld PRIVATE
    _SILENCE_CXX23_DENORM_DEPRECATION_WARNING
    WP_USE_ASSERT_TRACE
    APPLICATION_DLL_EXPORT
    $<$<CONFIG:Debug>:WP_USE_MEMLEAK_TRACKING>
    $<$<CONFIG:Profiling>:BW_PROFILING_BUILD>)

target_compile_definitions(BooleanWorld PRIVATE YAML_CPP_STATIC_DEFINE)

target_link_libraries(BooleanWorld PRIVATE
    core AppLib
    Willpower.Application Willpower.Collide Willpower.Common
    WillPower.Viz
    ext::mpp ext::mpp-helper ext::mpp-mesh ext::Utils
    ext::yaml-cpp)

target_compile_features(BooleanWorld PRIVATE cxx_std_23)
target_compile_options(BooleanWorld PRIVATE /W4)
bw_target_defaults(BooleanWorld)
bw_output_dirs(BooleanWorld "build/vs2026/bin/x64/$<CONFIG>" "build/vs2026/lib/x64/$<CONFIG>")
bw_enable_sdl_checks(BooleanWorld)
set_target_properties(BooleanWorld PROPERTIES FOLDER BooleanWorld)
