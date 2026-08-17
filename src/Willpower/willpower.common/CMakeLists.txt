# Adopted wholesale from tungsten-oxide (cpp/willpower/willpower.common).
#
# No longer links MassivePolyPusher or Utils: StringUtils, XmlReader/tinyxml2
# and FileSystem now live here, and DataNode/StructuredData - the YAML-backed
# replacement for utils::XmlNode - are new.

add_library(Willpower.Common SHARED
    src/AccelerationGrid.cpp
    src/BatchRenderable.cpp
    src/BezierSpline.cpp
    src/BoundingBox.cpp
    src/BoundingCircle.cpp
    src/BoundingConvexPolygon.cpp
    src/CachedDynamicAccelerationGrid.cpp
    src/CachedStaticAccelerationGrid.cpp
    src/CentripetalCatmullRomSpline.cpp
    src/CentripetalCatmullRomSplineLooping.cpp
    src/CubicBSpline.cpp
    src/CubicBSplineLooping.cpp
    src/DataNode.cpp
    src/DateTime.cpp
    src/DynamicAccelerationGrid.cpp
    src/ExtentsCalculator.cpp
    src/FileSystem.cpp
    src/IndexedTriangleBatchRenderable.cpp
    src/LineHit.cpp
    src/Logger.cpp
    src/MathsUtils.cpp
    src/Renderable.cpp
    src/SplinePath.cpp
    src/StackWalker.cpp
    src/StringUtils.cpp
    src/StructuredData.cpp
    src/Timer.cpp
    src/TriangleBatchRenderable.cpp
    src/TriangleStripBatchRenderable.cpp
    src/Triangulation.cpp
    src/Vector2.cpp
    src/WillpowerWalker.cpp
    src/XmlReader.cpp
    src/polypartition.cpp
    src/tinyxml2.cpp)

target_include_directories(Willpower.Common PUBLIC include)
bw_add_header_filter(Willpower.Common)

target_compile_definitions(Willpower.Common PRIVATE
    WIN32 _WINDOWS _USRDLL
    WP_COMMON_DLL_EXPORT
    WP_USE_ASSERT_TRACE
    TINYXML2_EXPORT
    $<$<CONFIG:Debug>:WP_USE_MEMLEAK_TRACKING>)

# vendor::headers supplies spline_library; nothing else is needed now.
target_link_libraries(Willpower.Common PUBLIC vendor::headers)

target_compile_features(Willpower.Common PRIVATE cxx_std_20)
target_compile_options(Willpower.Common PRIVATE /W4)
bw_target_defaults(Willpower.Common)
bw_output_dirs(Willpower.Common)
set_target_properties(Willpower.Common PROPERTIES FOLDER Willpower)

if(BUILD_TESTING)
    add_executable(willpower_acceleration_grid_set_operations_tests
        tests/acceleration_grid_set_operations_tests.cpp)
    target_link_libraries(willpower_acceleration_grid_set_operations_tests PRIVATE Willpower.Common)
    target_compile_features(willpower_acceleration_grid_set_operations_tests PRIVATE cxx_std_20)
    bw_target_defaults(willpower_acceleration_grid_set_operations_tests)
    bw_output_dirs(willpower_acceleration_grid_set_operations_tests)
    bw_deploy_runtime_dlls(willpower_acceleration_grid_set_operations_tests)
    add_test(NAME willpower_acceleration_grid_set_operations
             COMMAND willpower_acceleration_grid_set_operations_tests)

    add_executable(willpower_legacy_geometry_helper_removal_tests
        tests/legacy_geometry_helper_removal_tests.cpp)
    target_link_libraries(willpower_legacy_geometry_helper_removal_tests PRIVATE Willpower.Common)
    target_compile_features(willpower_legacy_geometry_helper_removal_tests PRIVATE cxx_std_20)
    bw_target_defaults(willpower_legacy_geometry_helper_removal_tests)
    bw_output_dirs(willpower_legacy_geometry_helper_removal_tests)
    bw_deploy_runtime_dlls(willpower_legacy_geometry_helper_removal_tests)
    add_test(NAME willpower_legacy_geometry_helper_removal
             COMMAND willpower_legacy_geometry_helper_removal_tests)

    add_executable(willpower_vector2_assignment_semantics_tests
        tests/vector2_assignment_semantics_tests.cpp)
    target_link_libraries(willpower_vector2_assignment_semantics_tests PRIVATE Willpower.Common)
    target_compile_features(willpower_vector2_assignment_semantics_tests PRIVATE cxx_std_20)
    bw_target_defaults(willpower_vector2_assignment_semantics_tests)
    bw_output_dirs(willpower_vector2_assignment_semantics_tests)
    bw_deploy_runtime_dlls(willpower_vector2_assignment_semantics_tests)
    add_test(NAME willpower_vector2_assignment_semantics
             COMMAND willpower_vector2_assignment_semantics_tests)

    add_executable(willpower_geometric_epsilon_tests
        tests/geometric_epsilon_tests.cpp)
    target_link_libraries(willpower_geometric_epsilon_tests PRIVATE Willpower.Common)
    target_compile_features(willpower_geometric_epsilon_tests PRIVATE cxx_std_20)
    bw_target_defaults(willpower_geometric_epsilon_tests)
    bw_output_dirs(willpower_geometric_epsilon_tests)
    bw_deploy_runtime_dlls(willpower_geometric_epsilon_tests)
    add_test(NAME willpower_geometric_epsilon
             COMMAND willpower_geometric_epsilon_tests)
endif()
