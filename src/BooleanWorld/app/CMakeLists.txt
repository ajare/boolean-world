# The game itself: a DLL that Launcher.exe loads at runtime.

add_library(BooleanWorld SHARED
    src/AcousticCatalog.cpp
    src/AcousticCatalogResourceDefinitionFactory.cpp
    src/AcousticPresetResolver.cpp
    src/CpuUpdateProfiler.cpp
    src/DLL.cpp
    src/EntityHandlerBooleanWorld.cpp
    src/LiquidReflectionSelection.cpp
    src/Map.cpp
    src/MapBooleanWorldDefinitionFactory.cpp
    src/PlayerLocation.cpp
    src/ProtoEntity.cpp
    src/ProtoEntityDefinitionFactory.cpp
    src/ReactiveCamera.cpp
    src/StateControllerBooleanWorld.cpp
    src/StateMapLoadBooleanWorld.cpp
    src/StateMapTransitionBooleanWorld.cpp
    src/StateMapUnloadBooleanWorld.cpp
    src/StatePlayBooleanWorld.cpp
    src/SteamAudio.cpp
    src/WorldCollisionSim.cpp
    # Vendored ImGui + ImPlot. Each BooleanWorld app carries its own copy;
    # this one and Launcher's are a newer ImGui than editor's.
    src/imgui/imgui.cpp
    src/imgui/imgui_demo.cpp
    src/imgui/imgui_draw.cpp
    src/imgui/imgui_tables.cpp
    src/imgui/imgui_widgets.cpp
    src/imgui/implot.cpp
    src/imgui/implot_demo.cpp
    src/imgui/implot_items.cpp)

target_include_directories(BooleanWorld PRIVATE include include/imgui)
bw_add_header_filter(BooleanWorld)

set(removed_app_headers
    DebugLineDataProvider.h
    MetricsDataProvider.h
    PrimitiveLineDataProvider.h
    ScrollingBuffer.h
    WorldTriangle.h)
foreach(header IN LISTS removed_app_headers)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/${header}")
        message(FATAL_ERROR "Unused app header must not remain: ${header}")
    endif()
endforeach()

target_compile_definitions(BooleanWorld PRIVATE
    _SILENCE_CXX23_DENORM_DEPRECATION_WARNING
    WP_USE_ASSERT_TRACE
    APPLICATION_DLL_EXPORT
    $<$<CONFIG:Debug>:WP_USE_MEMLEAK_TRACKING>)

target_compile_definitions(BooleanWorld PRIVATE YAML_CPP_STATIC_DEFINE)

target_link_libraries(BooleanWorld PRIVATE
    core core-lua AppLib BooleanWorldRender
    Willpower.Application Willpower.Collide Willpower.Common
    WillPower.Viz
    ext::mpp ext::mpp-helper ext::mpp-mesh ext::Utils
    ext::yaml-cpp vendor::opengl)

target_compile_features(BooleanWorld PRIVATE cxx_std_23)
target_compile_options(BooleanWorld PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
bw_target_defaults(BooleanWorld)
bw_output_dirs(BooleanWorld)
bw_enable_sdl_checks(BooleanWorld)
set_target_properties(BooleanWorld PROPERTIES FOLDER BooleanWorld)

if(BUILD_TESTING)
    add_executable(boolean_world_acoustic_catalog_tests
        tests/acoustic_catalog_tests.cpp
        src/AcousticCatalog.cpp
        src/AcousticCatalogResourceDefinitionFactory.cpp
        src/AcousticPresetResolver.cpp)
    target_include_directories(boolean_world_acoustic_catalog_tests PRIVATE include)
    target_link_libraries(boolean_world_acoustic_catalog_tests PRIVATE
        core AppLib BooleanWorldRender Willpower.Application Willpower.Common)
    target_compile_definitions(boolean_world_acoustic_catalog_tests PRIVATE
        YAML_CPP_STATIC_DEFINE)
    target_compile_features(boolean_world_acoustic_catalog_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_acoustic_catalog_tests)
    bw_output_dirs(boolean_world_acoustic_catalog_tests)
    bw_deploy_runtime_dlls(boolean_world_acoustic_catalog_tests)
    bw_deploy_vendor_dlls(boolean_world_acoustic_catalog_tests)
    add_test(NAME boolean_world_acoustic_catalog
             COMMAND boolean_world_acoustic_catalog_tests)

    add_executable(boolean_world_cpu_update_profiler_tests
        tests/cpu_update_profiler_tests.cpp
        src/CpuUpdateProfiler.cpp)
    target_include_directories(boolean_world_cpu_update_profiler_tests PRIVATE include)
    target_compile_features(boolean_world_cpu_update_profiler_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_cpu_update_profiler_tests)
    bw_output_dirs(boolean_world_cpu_update_profiler_tests)
    add_test(NAME boolean_world_cpu_update_profiler
             COMMAND boolean_world_cpu_update_profiler_tests)

    add_executable(boolean_world_clipping_record_tests
        tests/clipping_record_tests.cpp)
    target_include_directories(boolean_world_clipping_record_tests PRIVATE include)
    target_link_libraries(boolean_world_clipping_record_tests PRIVATE core)
    target_compile_features(boolean_world_clipping_record_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_clipping_record_tests)
    bw_output_dirs(boolean_world_clipping_record_tests)
    add_test(NAME boolean_world_clipping_record
             COMMAND boolean_world_clipping_record_tests)

    add_executable(boolean_world_dll_state_tests
        tests/dll_state_tests.cpp)
    target_include_directories(boolean_world_dll_state_tests PRIVATE include ../render/include)
    target_compile_features(boolean_world_dll_state_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_dll_state_tests)
    bw_output_dirs(boolean_world_dll_state_tests)
    add_test(NAME boolean_world_dll_state COMMAND boolean_world_dll_state_tests)

    add_executable(boolean_world_fragment_overdraw_tests
        tests/fragment_overdraw_tests.cpp)
    target_compile_definitions(boolean_world_fragment_overdraw_tests PRIVATE
        BW_APP_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_compile_features(boolean_world_fragment_overdraw_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_fragment_overdraw_tests)
    bw_output_dirs(boolean_world_fragment_overdraw_tests)
    add_test(NAME boolean_world_fragment_overdraw
             COMMAND boolean_world_fragment_overdraw_tests)

    add_executable(boolean_world_liquid_reflection_selection_tests
        tests/liquid_reflection_selection_tests.cpp
        src/LiquidReflectionSelection.cpp)
    target_include_directories(boolean_world_liquid_reflection_selection_tests
        PRIVATE include)
    target_link_libraries(boolean_world_liquid_reflection_selection_tests PRIVATE
        ext::mpp)
    target_compile_features(boolean_world_liquid_reflection_selection_tests
        PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_liquid_reflection_selection_tests)
    bw_output_dirs(boolean_world_liquid_reflection_selection_tests)
    add_test(NAME boolean_world_liquid_reflection_selection
             COMMAND boolean_world_liquid_reflection_selection_tests)

    add_executable(boolean_world_liquid_ssr_tests
        tests/liquid_ssr_tests.cpp)
    target_compile_definitions(boolean_world_liquid_ssr_tests PRIVATE
        BW_APP_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_compile_features(boolean_world_liquid_ssr_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_liquid_ssr_tests)
    bw_output_dirs(boolean_world_liquid_ssr_tests)
    add_test(NAME boolean_world_liquid_ssr
             COMMAND boolean_world_liquid_ssr_tests)

    add_executable(boolean_world_player_torch_shadow_tests
        tests/player_torch_shadow_tests.cpp)
    target_include_directories(boolean_world_player_torch_shadow_tests PRIVATE
        include ../render/include)
    target_link_libraries(boolean_world_player_torch_shadow_tests PRIVATE
        ext::mpp ext::mpp-data ext::mpp-helper ext::mpp-mesh ext::mpp-program
        ext::Utils)
    target_compile_definitions(boolean_world_player_torch_shadow_tests PRIVATE
        BW_APP_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_compile_features(boolean_world_player_torch_shadow_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_player_torch_shadow_tests)
    bw_output_dirs(boolean_world_player_torch_shadow_tests)
    bw_deploy_runtime_dlls(boolean_world_player_torch_shadow_tests)
    add_test(NAME boolean_world_player_torch_shadow
             COMMAND boolean_world_player_torch_shadow_tests)

    add_executable(boolean_world_map_transition_tests
        tests/map_transition_tests.cpp)
    target_include_directories(boolean_world_map_transition_tests PRIVATE
        include
        ${BW_ROOT}/src/AppLib/include
        ${BW_ROOT}/src/Willpower/willpower.application/include
        ${BW_ROOT}/src/Willpower/willpower.common/include)
    target_link_libraries(boolean_world_map_transition_tests PRIVATE AppLib)
    target_compile_features(boolean_world_map_transition_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_map_transition_tests)
    bw_output_dirs(boolean_world_map_transition_tests)
    bw_deploy_runtime_dlls(boolean_world_map_transition_tests)
    bw_deploy_vendor_dlls(boolean_world_map_transition_tests)
    add_test(NAME boolean_world_map_transition
             COMMAND boolean_world_map_transition_tests)

    add_executable(boolean_world_imgui_dll_boundary_state_tests
        tests/imgui_dll_boundary_state_tests.cpp
        src/imgui/imgui.cpp
        src/imgui/imgui_draw.cpp
        src/imgui/imgui_tables.cpp
        src/imgui/imgui_widgets.cpp
        src/imgui/implot.cpp
        src/imgui/implot_items.cpp)
    target_include_directories(boolean_world_imgui_dll_boundary_state_tests PRIVATE
        include include/imgui)
    target_compile_features(boolean_world_imgui_dll_boundary_state_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_imgui_dll_boundary_state_tests)
    bw_output_dirs(boolean_world_imgui_dll_boundary_state_tests)
    add_test(NAME boolean_world_imgui_dll_boundary_state
             COMMAND boolean_world_imgui_dll_boundary_state_tests)

    add_executable(boolean_world_entity_animation_tests
        tests/entity_animation_tests.cpp
        src/EntityHandlerBooleanWorld.cpp)
    target_include_directories(boolean_world_entity_animation_tests PRIVATE include)
    target_link_libraries(boolean_world_entity_animation_tests PRIVATE
        core AppLib Willpower.Application Willpower.Collide Willpower.Common)
    target_compile_features(boolean_world_entity_animation_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_entity_animation_tests)
    bw_output_dirs(boolean_world_entity_animation_tests)
    bw_deploy_runtime_dlls(boolean_world_entity_animation_tests)
    bw_deploy_vendor_dlls(boolean_world_entity_animation_tests)
    add_test(NAME boolean_world_entity_animation
             COMMAND boolean_world_entity_animation_tests)

    add_executable(boolean_world_collision_tests
        tests/world_collision_sim_tests.cpp
        src/PlayerLocation.cpp
        src/WorldCollisionSim.cpp)
    target_include_directories(boolean_world_collision_tests PRIVATE include)
    target_link_libraries(boolean_world_collision_tests PRIVATE
        core Willpower.Collide Willpower.Common)
    target_compile_features(boolean_world_collision_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_collision_tests)
    bw_output_dirs(boolean_world_collision_tests)
    bw_deploy_runtime_dlls(boolean_world_collision_tests)
    add_test(NAME boolean_world_collision COMMAND boolean_world_collision_tests)

    add_executable(boolean_world_player_pool_fall_tests
        tests/player_pool_fall_tests.cpp
        src/WorldCollisionSim.cpp)
    target_include_directories(boolean_world_player_pool_fall_tests PRIVATE
        include)
    target_link_libraries(boolean_world_player_pool_fall_tests PRIVATE
        core Willpower.Collide Willpower.Common)
    target_compile_features(boolean_world_player_pool_fall_tests PRIVATE
        cxx_std_23)
    bw_target_defaults(boolean_world_player_pool_fall_tests)
    bw_output_dirs(boolean_world_player_pool_fall_tests)
    bw_deploy_runtime_dlls(boolean_world_player_pool_fall_tests)
    add_test(NAME boolean_world_player_pool_fall
             COMMAND boolean_world_player_pool_fall_tests)

    add_executable(boolean_world_player_liquid_traversal_tests
        tests/player_liquid_traversal_tests.cpp)
    target_include_directories(boolean_world_player_liquid_traversal_tests PRIVATE
        include)
    target_link_libraries(boolean_world_player_liquid_traversal_tests PRIVATE
        core)
    target_compile_features(boolean_world_player_liquid_traversal_tests PRIVATE
        cxx_std_23)
    bw_target_defaults(boolean_world_player_liquid_traversal_tests)
    bw_output_dirs(boolean_world_player_liquid_traversal_tests)
    add_test(NAME boolean_world_player_liquid_traversal
             COMMAND boolean_world_player_liquid_traversal_tests)

    add_executable(boolean_world_player_view_tests
        tests/player_view_tests.cpp
        src/ReactiveCamera.cpp)
    target_include_directories(boolean_world_player_view_tests PRIVATE include)
    target_link_libraries(boolean_world_player_view_tests PRIVATE
        core Willpower.Common
        ext::mpp ext::mpp-data ext::mpp-helper ext::mpp-mesh ext::mpp-program
        ext::Utils)
    target_compile_features(boolean_world_player_view_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_player_view_tests)
    bw_output_dirs(boolean_world_player_view_tests)
    bw_deploy_runtime_dlls(boolean_world_player_view_tests)
    add_test(NAME boolean_world_player_view COMMAND boolean_world_player_view_tests)

    add_executable(boolean_world_triangle_data_provider_benchmark
        tests/world_triangle_3d_data_provider_benchmark.cpp
        ../render/src/WorldTriangle3dDataProvider.cpp)
    target_include_directories(boolean_world_triangle_data_provider_benchmark PRIVATE include ../render/include)
    target_link_libraries(boolean_world_triangle_data_provider_benchmark PRIVATE
        ext::mpp ext::mpp-data ext::mpp-helper ext::mpp-mesh ext::mpp-program
        ext::Utils)
    target_compile_features(boolean_world_triangle_data_provider_benchmark PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_triangle_data_provider_benchmark)
    bw_output_dirs(boolean_world_triangle_data_provider_benchmark)
    bw_deploy_runtime_dlls(boolean_world_triangle_data_provider_benchmark)

    add_executable(boolean_world_triangle_data_provider_tests
        tests/world_triangle_3d_data_provider_tests.cpp
        ../render/src/WorldTriangle3dDataProvider.cpp)
    target_include_directories(boolean_world_triangle_data_provider_tests PRIVATE include ../render/include)
    target_link_libraries(boolean_world_triangle_data_provider_tests PRIVATE
        ext::mpp ext::mpp-data ext::mpp-helper ext::mpp-mesh ext::mpp-program
        ext::Utils)
    target_compile_features(boolean_world_triangle_data_provider_tests PRIVATE cxx_std_23)
    target_compile_options(boolean_world_triangle_data_provider_tests PRIVATE
        /fsanitize=address /Zi)
    target_link_options(boolean_world_triangle_data_provider_tests PRIVATE
        /INFERASANLIBS /DEBUG)
    bw_target_defaults(boolean_world_triangle_data_provider_tests)
    bw_output_dirs(boolean_world_triangle_data_provider_tests)
    bw_deploy_runtime_dlls(boolean_world_triangle_data_provider_tests)
    get_filename_component(msvc_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    add_custom_command(TARGET boolean_world_triangle_data_provider_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${msvc_bin_dir}/clang_rt.asan_dynamic-x86_64.dll"
                "$<TARGET_FILE_DIR:boolean_world_triangle_data_provider_tests>")
    add_test(NAME boolean_world_triangle_data_provider
             COMMAND boolean_world_triangle_data_provider_tests)

    add_executable(boolean_world_map_load_tests
        tests/map_load_tests.cpp
        src/Map.cpp)
    target_include_directories(boolean_world_map_load_tests PRIVATE include)
    target_link_libraries(boolean_world_map_load_tests PRIVATE
        core core-lua AppLib BooleanWorldRender Willpower.Application Willpower.Common)
    target_compile_definitions(boolean_world_map_load_tests PRIVATE
        BW_MAP_TEST_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_compile_features(boolean_world_map_load_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_map_load_tests)
    bw_output_dirs(boolean_world_map_load_tests)
    bw_deploy_runtime_dlls(boolean_world_map_load_tests)
    bw_deploy_vendor_dlls(boolean_world_map_load_tests)
    add_test(NAME boolean_world_map_load COMMAND boolean_world_map_load_tests)

    add_executable(boolean_world_proc_material_load_tests
        tests/proc_material_load_tests.cpp
        ../render/src/EmbossingCatalog.cpp
        ../render/src/EmbossingCatalogResourceDefinitionFactory.cpp
        ../render/src/ProcMaterial.cpp
        ../render/src/ProcMaterialResourceDefinitionFactory.cpp)
    target_include_directories(boolean_world_proc_material_load_tests PRIVATE include ../render/include)
    target_link_libraries(boolean_world_proc_material_load_tests PRIVATE
        core AppLib Willpower.Application Willpower.Common)
    target_compile_features(boolean_world_proc_material_load_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_proc_material_load_tests)
    bw_output_dirs(boolean_world_proc_material_load_tests)
    bw_deploy_runtime_dlls(boolean_world_proc_material_load_tests)
    bw_deploy_vendor_dlls(boolean_world_proc_material_load_tests)
    add_test(NAME boolean_world_proc_material_load_fixture-load
             COMMAND boolean_world_proc_material_load_tests fixture-load)
    add_test(NAME boolean_world_proc_material_load_duplicate-id
             COMMAND boolean_world_proc_material_load_tests duplicate-id)
    add_test(NAME boolean_world_embossing_catalog_load_singleton
             COMMAND boolean_world_proc_material_load_tests emboss-singleton)

    add_executable(boolean_world_sub_material_resolver_tests
        tests/sub_material_resolver_tests.cpp
        ../render/src/EmbossingCatalog.cpp
        ../render/src/EmbossingCatalogResourceDefinitionFactory.cpp
        ../render/src/ProcMaterial.cpp
        ../render/src/ProcMaterialResourceDefinitionFactory.cpp
        ../render/src/SubMaterialResolver.cpp)
    target_include_directories(boolean_world_sub_material_resolver_tests PRIVATE include ../render/include)
    target_link_libraries(boolean_world_sub_material_resolver_tests PRIVATE
        core AppLib Willpower.Application Willpower.Common)
    target_compile_features(boolean_world_sub_material_resolver_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_sub_material_resolver_tests)
    bw_output_dirs(boolean_world_sub_material_resolver_tests)
    bw_deploy_runtime_dlls(boolean_world_sub_material_resolver_tests)
    bw_deploy_vendor_dlls(boolean_world_sub_material_resolver_tests)
    add_test(NAME boolean_world_sub_material_resolver_resolved
             COMMAND boolean_world_sub_material_resolver_tests resolved)
    add_test(NAME boolean_world_sub_material_resolver_fallback
             COMMAND boolean_world_sub_material_resolver_tests fallback)
endif()
