# The BooleanWorld engine core, linked statically into every app here.

add_library(core STATIC
    src/AnimatedProperty.cpp
    src/Arrangement.cpp
    src/ArrangementWorldData.cpp
    src/ArrangementWorldDataGenerator.cpp
    src/CirclePolygon.cpp
    src/CircleSegmentPolygon.cpp
    src/DefaultWorldDataGenerator.cpp
    src/DynamicWorldDataGenerator.cpp
    src/Easing.cpp
    src/InfluenceEye.cpp
    src/MaterialDefinition.cpp
    src/MeshPrimitive.cpp
    src/Module.cpp
    src/Primitive.cpp
    src/PrimitiveGroup.cpp
    src/PrimitivePropertySet.cpp
    src/RectanglePolygon.cpp
    src/RegularPolygon.cpp
    src/Serializable.cpp
    src/Serializer.cpp
    src/SquareTiling.cpp
    src/SuperformulaPolygon.cpp
    src/Tiling.cpp
    src/TorusPolygon.cpp
    src/TorusSegmentPolygon.cpp
    src/TransformFlow.cpp
    src/Triangulator.cpp
    src/Utils.cpp
    src/Vertex.cpp
    src/VertexDistribution.cpp
    src/VertexTransformer.cpp
    src/VertexTransformerObject.cpp
    src/World.cpp
    src/WorldDataGenerator.cpp
    src/WorldTriggerLine.cpp
    src/YamlSerializer.cpp
    src/tTransform.cpp)

target_include_directories(core PUBLIC include)
bw_add_header_filter(core
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../common/include")

target_compile_definitions(core PRIVATE YAML_CPP_STATIC_DEFINE _CONSOLE)

target_link_libraries(core PUBLIC
    bw-common Willpower.Common
    vendor::concurrencpp ext::yaml-cpp)

target_compile_features(core PRIVATE cxx_std_23)
target_compile_options(core PRIVATE /W3)
bw_target_defaults(core)
bw_output_dirs(core)
bw_enable_sdl_checks(core)
set_target_properties(core PROPERTIES FOLDER BooleanWorld)

if(BUILD_TESTING)
    add_executable(boolean_world_arrangement_broad_phase_benchmark
        tests/arrangement_broad_phase_benchmark.cpp)
    target_link_libraries(boolean_world_arrangement_broad_phase_benchmark PRIVATE core)
    target_compile_features(boolean_world_arrangement_broad_phase_benchmark PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_arrangement_broad_phase_benchmark)
    bw_output_dirs(boolean_world_arrangement_broad_phase_benchmark)
    bw_deploy_runtime_dlls(boolean_world_arrangement_broad_phase_benchmark)

    add_executable(boolean_world_arrangement_broad_phase_tests
        tests/arrangement_broad_phase_tests.cpp)
    target_link_libraries(boolean_world_arrangement_broad_phase_tests PRIVATE core)
    target_compile_features(boolean_world_arrangement_broad_phase_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_arrangement_broad_phase_tests)
    bw_output_dirs(boolean_world_arrangement_broad_phase_tests)
    bw_deploy_runtime_dlls(boolean_world_arrangement_broad_phase_tests)
    add_test(NAME boolean_world_arrangement_broad_phase COMMAND boolean_world_arrangement_broad_phase_tests)

    add_executable(boolean_world_interpolator_tests
        tests/interpolator_tests.cpp)
    target_link_libraries(boolean_world_interpolator_tests PRIVATE core)
    target_compile_features(boolean_world_interpolator_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_interpolator_tests)
    bw_output_dirs(boolean_world_interpolator_tests)
    bw_deploy_runtime_dlls(boolean_world_interpolator_tests)
    add_test(NAME boolean_world_interpolator COMMAND boolean_world_interpolator_tests)

    add_executable(boolean_world_vertex_transformer_serialization_tests
        tests/vertex_transformer_serialization_tests.cpp)
    target_link_libraries(boolean_world_vertex_transformer_serialization_tests PRIVATE core)
    target_compile_features(boolean_world_vertex_transformer_serialization_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_vertex_transformer_serialization_tests)
    bw_output_dirs(boolean_world_vertex_transformer_serialization_tests)
    bw_deploy_runtime_dlls(boolean_world_vertex_transformer_serialization_tests)
    add_test(NAME boolean_world_vertex_transformer_serialization COMMAND boolean_world_vertex_transformer_serialization_tests)

    add_executable(boolean_world_value_object_copy_tests
        tests/value_object_copy_tests.cpp)
    target_link_libraries(boolean_world_value_object_copy_tests PRIVATE core)
    target_compile_features(boolean_world_value_object_copy_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_value_object_copy_tests)
    bw_output_dirs(boolean_world_value_object_copy_tests)
    bw_deploy_runtime_dlls(boolean_world_value_object_copy_tests)
    add_test(NAME boolean_world_value_object_copy COMMAND boolean_world_value_object_copy_tests)

    add_executable(boolean_world_world_clear_tests
        tests/world_clear_tests.cpp)
    target_link_libraries(boolean_world_world_clear_tests PRIVATE core)
    target_compile_features(boolean_world_world_clear_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_world_clear_tests)
    bw_output_dirs(boolean_world_world_clear_tests)
    bw_deploy_runtime_dlls(boolean_world_world_clear_tests)
    add_test(NAME boolean_world_world_clear COMMAND boolean_world_world_clear_tests)

    add_executable(boolean_world_world_reload_tests
        tests/world_reload_tests.cpp)
    target_link_libraries(boolean_world_world_reload_tests PRIVATE core)
    target_compile_features(boolean_world_world_reload_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_world_reload_tests)
    bw_output_dirs(boolean_world_world_reload_tests)
    bw_deploy_runtime_dlls(boolean_world_world_reload_tests)
    add_test(NAME boolean_world_world_reload COMMAND boolean_world_world_reload_tests)

    add_executable(boolean_world_primitive_triangulation_tests
        tests/primitive_triangulation_tests.cpp)
    target_link_libraries(boolean_world_primitive_triangulation_tests PRIVATE core)
    target_compile_features(boolean_world_primitive_triangulation_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_primitive_triangulation_tests)
    bw_output_dirs(boolean_world_primitive_triangulation_tests)
    bw_deploy_runtime_dlls(boolean_world_primitive_triangulation_tests)
    add_test(NAME boolean_world_primitive_triangulation COMMAND boolean_world_primitive_triangulation_tests)
endif()
