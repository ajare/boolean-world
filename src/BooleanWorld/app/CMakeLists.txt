# The game itself: a DLL that Launcher.exe loads at runtime.

add_library(BooleanWorld SHARED
    src/DLL.cpp
    src/EntityHandlerBooleanWorld.cpp
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
    src/WorldWallOrientation.cpp
    src/WorldTriangle3dDataProvider.cpp
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

target_compile_definitions(BooleanWorld PRIVATE
    _SILENCE_CXX23_DENORM_DEPRECATION_WARNING
    WP_USE_ASSERT_TRACE
    APPLICATION_DLL_EXPORT
    $<$<CONFIG:Debug>:WP_USE_MEMLEAK_TRACKING>)

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
bw_output_dirs(BooleanWorld)
bw_enable_sdl_checks(BooleanWorld)
set_target_properties(BooleanWorld PROPERTIES FOLDER BooleanWorld)

if(BUILD_TESTING)
    add_executable(boolean_world_clipping_record_tests
        tests/clipping_record_tests.cpp)
    target_include_directories(boolean_world_clipping_record_tests PRIVATE include)
    target_link_libraries(boolean_world_clipping_record_tests PRIVATE core)
    target_compile_features(boolean_world_clipping_record_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_clipping_record_tests)
    bw_output_dirs(boolean_world_clipping_record_tests)
    add_test(NAME boolean_world_clipping_record
             COMMAND boolean_world_clipping_record_tests)

    add_executable(boolean_world_collision_tests
        tests/world_collision_sim_tests.cpp
        src/WorldCollisionSim.cpp)
    target_include_directories(boolean_world_collision_tests PRIVATE include)
    target_link_libraries(boolean_world_collision_tests PRIVATE
        core Willpower.Collide Willpower.Common)
    target_compile_definitions(boolean_world_collision_tests PRIVATE
        BW_COLLISION_TEST_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_compile_features(boolean_world_collision_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_collision_tests)
    bw_output_dirs(boolean_world_collision_tests)
    bw_deploy_runtime_dlls(boolean_world_collision_tests)
    add_test(NAME boolean_world_collision COMMAND boolean_world_collision_tests)

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

    add_executable(boolean_world_triangle_data_provider_tests
        tests/world_triangle_3d_data_provider_tests.cpp
        src/WorldTriangle3dDataProvider.cpp)
    target_include_directories(boolean_world_triangle_data_provider_tests PRIVATE include)
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

    add_executable(boolean_world_wall_orientation_tests
        tests/world_wall_orientation_tests.cpp
        src/WorldWallOrientation.cpp)
    target_include_directories(boolean_world_wall_orientation_tests PRIVATE include)
    target_link_libraries(boolean_world_wall_orientation_tests PRIVATE
        core Willpower.Common)
    target_compile_features(boolean_world_wall_orientation_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_wall_orientation_tests)
    bw_output_dirs(boolean_world_wall_orientation_tests)
    bw_deploy_runtime_dlls(boolean_world_wall_orientation_tests)
    add_test(NAME boolean_world_wall_orientation
             COMMAND boolean_world_wall_orientation_tests)

    add_executable(boolean_world_map_load_tests
        tests/map_load_tests.cpp
        src/Map.cpp)
    target_include_directories(boolean_world_map_load_tests PRIVATE include)
    target_link_libraries(boolean_world_map_load_tests PRIVATE
        core AppLib Willpower.Application Willpower.Common)
    target_compile_definitions(boolean_world_map_load_tests PRIVATE
        BW_MAP_TEST_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_compile_features(boolean_world_map_load_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_map_load_tests)
    bw_output_dirs(boolean_world_map_load_tests)
    bw_deploy_runtime_dlls(boolean_world_map_load_tests)
    bw_deploy_vendor_dlls(boolean_world_map_load_tests)
    add_test(NAME boolean_world_map_load COMMAND boolean_world_map_load_tests)
endif()
