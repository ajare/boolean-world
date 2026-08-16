add_library(Willpower.Collide SHARED
    src/CollideAABB.cpp
    src/Collider.cpp
    src/ColliderCircle.cpp
    src/Simulation.cpp
    src/StaticLine.cpp)

target_include_directories(Willpower.Collide PUBLIC include)
bw_add_header_filter(Willpower.Collide)

target_compile_definitions(Willpower.Collide PRIVATE
    WIN32 _WINDOWS _USRDLL
    WP_COLLIDE_DLL_EXPORT
    WP_USE_ASSERT_TRACE
    $<$<CONFIG:Debug>:WP_USE_MEMLEAK_TRACKING>)

target_link_libraries(Willpower.Collide
    PUBLIC  Willpower.Common Willpower.Geometry
    PRIVATE ext::Utils vendor::headers)

target_compile_features(Willpower.Collide PRIVATE cxx_std_20)
target_compile_options(Willpower.Collide PRIVATE /W4)
bw_target_defaults(Willpower.Collide)
bw_output_dirs(Willpower.Collide)
set_target_properties(Willpower.Collide PROPERTIES FOLDER Willpower)

if(BUILD_TESTING)
    add_executable(willpower_simulation_ownership_tests
        tests/simulation_ownership_tests.cpp)
    target_link_libraries(willpower_simulation_ownership_tests PRIVATE Willpower.Collide)
    target_compile_features(willpower_simulation_ownership_tests PRIVATE cxx_std_20)
    bw_target_defaults(willpower_simulation_ownership_tests)
    bw_output_dirs(willpower_simulation_ownership_tests)
    bw_deploy_runtime_dlls(willpower_simulation_ownership_tests)
    add_test(NAME willpower_simulation_ownership
             COMMAND willpower_simulation_ownership_tests)

    add_executable(willpower_static_line_clipping_tests
        tests/static_line_clipping_tests.cpp)
    target_link_libraries(willpower_static_line_clipping_tests PRIVATE Willpower.Collide)
    target_compile_features(willpower_static_line_clipping_tests PRIVATE cxx_std_20)
    bw_target_defaults(willpower_static_line_clipping_tests)
    bw_output_dirs(willpower_static_line_clipping_tests)
    bw_deploy_runtime_dlls(willpower_static_line_clipping_tests)
    add_test(NAME willpower_static_line_clipping
             COMMAND willpower_static_line_clipping_tests)

    add_executable(willpower_simulation_order_tests
        tests/simulation_order_tests.cpp)
    target_link_libraries(willpower_simulation_order_tests PRIVATE Willpower.Collide)
    target_compile_features(willpower_simulation_order_tests PRIVATE cxx_std_20)
    bw_target_defaults(willpower_simulation_order_tests)
    bw_output_dirs(willpower_simulation_order_tests)
    bw_deploy_runtime_dlls(willpower_simulation_order_tests)
    add_test(NAME willpower_simulation_order
             COMMAND willpower_simulation_order_tests)
endif()
