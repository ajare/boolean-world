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

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/core/PrimitiveGroup.h")
    message(FATAL_ERROR "PrimitiveGroup must not remain in the core public interface")
endif()

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
    add_executable(boolean_world_world_primitive_picking_benchmark
        tests/world_primitive_picking_benchmark.cpp)
    target_link_libraries(boolean_world_world_primitive_picking_benchmark PRIVATE core)
    target_compile_features(boolean_world_world_primitive_picking_benchmark PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_world_primitive_picking_benchmark)
    bw_output_dirs(boolean_world_world_primitive_picking_benchmark)
    bw_deploy_runtime_dlls(boolean_world_world_primitive_picking_benchmark)

    add_executable(boolean_world_world_primitive_removal_benchmark
        tests/world_primitive_removal_benchmark.cpp)
    target_link_libraries(boolean_world_world_primitive_removal_benchmark PRIVATE core)
    target_compile_features(boolean_world_world_primitive_removal_benchmark PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_world_primitive_removal_benchmark)
    bw_output_dirs(boolean_world_world_primitive_removal_benchmark)
    bw_deploy_runtime_dlls(boolean_world_world_primitive_removal_benchmark)

    add_executable(boolean_world_arrangement_vertex_lookup_benchmark
        tests/arrangement_vertex_lookup_benchmark.cpp)
    target_link_libraries(boolean_world_arrangement_vertex_lookup_benchmark PRIVATE core)
    target_compile_features(boolean_world_arrangement_vertex_lookup_benchmark PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_arrangement_vertex_lookup_benchmark)
    bw_output_dirs(boolean_world_arrangement_vertex_lookup_benchmark)
    bw_deploy_runtime_dlls(boolean_world_arrangement_vertex_lookup_benchmark)

    add_executable(boolean_world_arrangement_hierarchy_benchmark
        tests/arrangement_hierarchy_benchmark.cpp)
    target_link_libraries(boolean_world_arrangement_hierarchy_benchmark PRIVATE core)
    target_compile_features(boolean_world_arrangement_hierarchy_benchmark PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_arrangement_hierarchy_benchmark)
    bw_output_dirs(boolean_world_arrangement_hierarchy_benchmark)
    bw_deploy_runtime_dlls(boolean_world_arrangement_hierarchy_benchmark)

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

    add_executable(boolean_world_arrangement_leaf_boundary_tests
        tests/arrangement_leaf_boundary_tests.cpp)
    target_link_libraries(boolean_world_arrangement_leaf_boundary_tests PRIVATE core)
    target_compile_features(boolean_world_arrangement_leaf_boundary_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_arrangement_leaf_boundary_tests)
    bw_output_dirs(boolean_world_arrangement_leaf_boundary_tests)
    bw_deploy_runtime_dlls(boolean_world_arrangement_leaf_boundary_tests)
    add_test(NAME boolean_world_arrangement_leaf_boundary COMMAND boolean_world_arrangement_leaf_boundary_tests)

    add_executable(boolean_world_arrangement_hierarchy_tests
        tests/arrangement_hierarchy_tests.cpp)
    target_link_libraries(boolean_world_arrangement_hierarchy_tests PRIVATE core)
    target_compile_features(boolean_world_arrangement_hierarchy_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_arrangement_hierarchy_tests)
    bw_output_dirs(boolean_world_arrangement_hierarchy_tests)
    bw_deploy_runtime_dlls(boolean_world_arrangement_hierarchy_tests)
    add_test(NAME boolean_world_arrangement_hierarchy COMMAND boolean_world_arrangement_hierarchy_tests)

    add_executable(boolean_world_arrangement_sample_point_tests
        tests/arrangement_sample_point_tests.cpp)
    target_link_libraries(boolean_world_arrangement_sample_point_tests PRIVATE core)
    target_compile_features(boolean_world_arrangement_sample_point_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_arrangement_sample_point_tests)
    bw_output_dirs(boolean_world_arrangement_sample_point_tests)
    bw_deploy_runtime_dlls(boolean_world_arrangement_sample_point_tests)
    add_test(NAME boolean_world_arrangement_sample_point COMMAND boolean_world_arrangement_sample_point_tests)

    add_executable(boolean_world_arrangement_same_face_edge_tests
        tests/arrangement_same_face_edge_tests.cpp)
    target_link_libraries(boolean_world_arrangement_same_face_edge_tests PRIVATE core)
    target_compile_features(boolean_world_arrangement_same_face_edge_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_arrangement_same_face_edge_tests)
    bw_output_dirs(boolean_world_arrangement_same_face_edge_tests)
    bw_deploy_runtime_dlls(boolean_world_arrangement_same_face_edge_tests)
    add_test(NAME boolean_world_arrangement_same_face_edge COMMAND boolean_world_arrangement_same_face_edge_tests)

    add_executable(boolean_world_arrangement_winning_primitive_tests
        tests/arrangement_winning_primitive_tests.cpp)
    target_link_libraries(boolean_world_arrangement_winning_primitive_tests PRIVATE core)
    target_compile_features(boolean_world_arrangement_winning_primitive_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_arrangement_winning_primitive_tests)
    bw_output_dirs(boolean_world_arrangement_winning_primitive_tests)
    bw_deploy_runtime_dlls(boolean_world_arrangement_winning_primitive_tests)
    add_test(NAME boolean_world_arrangement_winning_primitive COMMAND boolean_world_arrangement_winning_primitive_tests)

    add_executable(boolean_world_arrangement_triangulation_precision_tests
        tests/arrangement_triangulation_precision_tests.cpp)
    target_link_libraries(boolean_world_arrangement_triangulation_precision_tests PRIVATE core)
    target_compile_features(boolean_world_arrangement_triangulation_precision_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_arrangement_triangulation_precision_tests)
    bw_output_dirs(boolean_world_arrangement_triangulation_precision_tests)
    bw_deploy_runtime_dlls(boolean_world_arrangement_triangulation_precision_tests)
    add_test(NAME boolean_world_arrangement_triangulation_precision COMMAND boolean_world_arrangement_triangulation_precision_tests)

    add_executable(boolean_world_arrangement_vertex_lookup_tests
        tests/arrangement_vertex_lookup_tests.cpp)
    target_link_libraries(boolean_world_arrangement_vertex_lookup_tests PRIVATE core)
    target_compile_features(boolean_world_arrangement_vertex_lookup_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_arrangement_vertex_lookup_tests)
    bw_output_dirs(boolean_world_arrangement_vertex_lookup_tests)
    bw_deploy_runtime_dlls(boolean_world_arrangement_vertex_lookup_tests)
    add_test(NAME boolean_world_arrangement_vertex_lookup COMMAND boolean_world_arrangement_vertex_lookup_tests)

    add_executable(boolean_world_arrangement_statistics_tests
        tests/arrangement_statistics_tests.cpp)
    target_link_libraries(boolean_world_arrangement_statistics_tests PRIVATE core)
    target_compile_features(boolean_world_arrangement_statistics_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_arrangement_statistics_tests)
    bw_output_dirs(boolean_world_arrangement_statistics_tests)
    bw_deploy_runtime_dlls(boolean_world_arrangement_statistics_tests)
    add_test(NAME boolean_world_arrangement_statistics COMMAND boolean_world_arrangement_statistics_tests)

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

    add_executable(boolean_world_transform_serialization_tests
        tests/transform_serialization_tests.cpp)
    target_link_libraries(boolean_world_transform_serialization_tests PRIVATE core)
    target_compile_features(boolean_world_transform_serialization_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_transform_serialization_tests)
    bw_output_dirs(boolean_world_transform_serialization_tests)
    bw_deploy_runtime_dlls(boolean_world_transform_serialization_tests)
    add_test(NAME boolean_world_transform_serialization COMMAND boolean_world_transform_serialization_tests)

    add_executable(boolean_world_trigger_line_operand_tests
        tests/trigger_line_operand_tests.cpp)
    target_link_libraries(boolean_world_trigger_line_operand_tests PRIVATE core)
    target_compile_features(boolean_world_trigger_line_operand_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_trigger_line_operand_tests)
    bw_output_dirs(boolean_world_trigger_line_operand_tests)
    bw_deploy_runtime_dlls(boolean_world_trigger_line_operand_tests)
    add_test(NAME boolean_world_trigger_line_operand COMMAND boolean_world_trigger_line_operand_tests)

    add_executable(boolean_world_yaml_serializer_vector2_tests
        tests/yaml_serializer_vector2_tests.cpp)
    target_link_libraries(boolean_world_yaml_serializer_vector2_tests PRIVATE core)
    target_compile_features(boolean_world_yaml_serializer_vector2_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_yaml_serializer_vector2_tests)
    bw_output_dirs(boolean_world_yaml_serializer_vector2_tests)
    bw_deploy_runtime_dlls(boolean_world_yaml_serializer_vector2_tests)
    add_test(NAME boolean_world_yaml_serializer_vector2 COMMAND boolean_world_yaml_serializer_vector2_tests)

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

    add_executable(boolean_world_world_assignment_tests
        tests/world_assignment_tests.cpp)
    target_link_libraries(boolean_world_world_assignment_tests PRIVATE core)
    target_compile_features(boolean_world_world_assignment_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_world_assignment_tests)
    bw_output_dirs(boolean_world_world_assignment_tests)
    bw_deploy_runtime_dlls(boolean_world_world_assignment_tests)
    add_test(NAME boolean_world_world_assignment COMMAND boolean_world_world_assignment_tests)

    add_executable(boolean_world_world_data_generator_ownership_tests
        tests/world_data_generator_ownership_tests.cpp)
    target_link_libraries(boolean_world_world_data_generator_ownership_tests PRIVATE core)
    target_compile_features(boolean_world_world_data_generator_ownership_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_world_data_generator_ownership_tests)
    bw_output_dirs(boolean_world_world_data_generator_ownership_tests)
    bw_deploy_runtime_dlls(boolean_world_world_data_generator_ownership_tests)
    add_test(NAME boolean_world_world_data_generator_ownership COMMAND boolean_world_world_data_generator_ownership_tests)

    add_executable(boolean_world_world_data_access_tests
        tests/world_data_access_tests.cpp)
    target_link_libraries(boolean_world_world_data_access_tests PRIVATE core)
    target_compile_features(boolean_world_world_data_access_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_world_data_access_tests)
    bw_output_dirs(boolean_world_world_data_access_tests)
    bw_deploy_runtime_dlls(boolean_world_world_data_access_tests)
    add_test(NAME boolean_world_world_data_access COMMAND boolean_world_world_data_access_tests)

    add_executable(boolean_world_grid_metadata_access_tests
        tests/grid_metadata_access_tests.cpp)
    target_link_libraries(boolean_world_grid_metadata_access_tests PRIVATE core)
    target_compile_features(boolean_world_grid_metadata_access_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_grid_metadata_access_tests)
    bw_output_dirs(boolean_world_grid_metadata_access_tests)
    bw_deploy_runtime_dlls(boolean_world_grid_metadata_access_tests)
    add_test(NAME boolean_world_grid_metadata_access COMMAND boolean_world_grid_metadata_access_tests)

    add_executable(boolean_world_world_reload_tests
        tests/world_reload_tests.cpp)
    target_link_libraries(boolean_world_world_reload_tests PRIVATE core)
    target_compile_features(boolean_world_world_reload_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_world_reload_tests)
    bw_output_dirs(boolean_world_world_reload_tests)
    bw_deploy_runtime_dlls(boolean_world_world_reload_tests)
    add_test(NAME boolean_world_world_reload COMMAND boolean_world_world_reload_tests)

    add_executable(boolean_world_world_primitive_picking_tests
        tests/world_primitive_picking_tests.cpp)
    target_link_libraries(boolean_world_world_primitive_picking_tests PRIVATE core)
    target_compile_features(boolean_world_world_primitive_picking_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_world_primitive_picking_tests)
    bw_output_dirs(boolean_world_world_primitive_picking_tests)
    bw_deploy_runtime_dlls(boolean_world_world_primitive_picking_tests)
    add_test(NAME boolean_world_world_primitive_picking COMMAND boolean_world_world_primitive_picking_tests)

    add_executable(boolean_world_world_primitive_membership_tests
        tests/world_primitive_membership_tests.cpp)
    target_link_libraries(boolean_world_world_primitive_membership_tests PRIVATE core)
    target_compile_features(boolean_world_world_primitive_membership_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_world_primitive_membership_tests)
    bw_output_dirs(boolean_world_world_primitive_membership_tests)
    bw_deploy_runtime_dlls(boolean_world_world_primitive_membership_tests)
    add_test(NAME boolean_world_world_primitive_membership COMMAND boolean_world_world_primitive_membership_tests)

    add_executable(boolean_world_world_primitive_removal_tests
        tests/world_primitive_removal_tests.cpp)
    target_link_libraries(boolean_world_world_primitive_removal_tests PRIVATE core)
    target_compile_features(boolean_world_world_primitive_removal_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_world_primitive_removal_tests)
    bw_output_dirs(boolean_world_world_primitive_removal_tests)
    bw_deploy_runtime_dlls(boolean_world_world_primitive_removal_tests)
    add_test(NAME boolean_world_world_primitive_removal COMMAND boolean_world_world_primitive_removal_tests)

    add_executable(boolean_world_primitive_triangulation_tests
        tests/primitive_triangulation_tests.cpp)
    target_link_libraries(boolean_world_primitive_triangulation_tests PRIVATE core)
    target_compile_features(boolean_world_primitive_triangulation_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_primitive_triangulation_tests)
    bw_output_dirs(boolean_world_primitive_triangulation_tests)
    bw_deploy_runtime_dlls(boolean_world_primitive_triangulation_tests)
    add_test(NAME boolean_world_primitive_triangulation COMMAND boolean_world_primitive_triangulation_tests)

    add_executable(boolean_world_primitive_time_visibility_tests
        tests/primitive_time_visibility_tests.cpp)
    target_link_libraries(boolean_world_primitive_time_visibility_tests PRIVATE core)
    target_compile_features(boolean_world_primitive_time_visibility_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_primitive_time_visibility_tests)
    bw_output_dirs(boolean_world_primitive_time_visibility_tests)
    bw_deploy_runtime_dlls(boolean_world_primitive_time_visibility_tests)
    add_test(NAME boolean_world_primitive_time_visibility COMMAND boolean_world_primitive_time_visibility_tests)

    add_executable(boolean_world_primitive_group_removal_tests
        tests/primitive_group_removal_tests.cpp)
    target_link_libraries(boolean_world_primitive_group_removal_tests PRIVATE core)
    target_compile_features(boolean_world_primitive_group_removal_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_primitive_group_removal_tests)
    bw_output_dirs(boolean_world_primitive_group_removal_tests)
    bw_deploy_runtime_dlls(boolean_world_primitive_group_removal_tests)
    add_test(NAME boolean_world_primitive_group_removal COMMAND boolean_world_primitive_group_removal_tests)

    add_executable(boolean_world_derived_geometry_invalidation_tests
        tests/derived_geometry_invalidation_tests.cpp)
    target_link_libraries(boolean_world_derived_geometry_invalidation_tests PRIVATE core)
    target_compile_features(boolean_world_derived_geometry_invalidation_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_derived_geometry_invalidation_tests)
    bw_output_dirs(boolean_world_derived_geometry_invalidation_tests)
    bw_deploy_runtime_dlls(boolean_world_derived_geometry_invalidation_tests)
    add_test(NAME boolean_world_derived_geometry_invalidation COMMAND boolean_world_derived_geometry_invalidation_tests)

    add_executable(boolean_world_primitive_bounds_tests
        tests/primitive_bounds_tests.cpp)
    target_link_libraries(boolean_world_primitive_bounds_tests PRIVATE core)
    target_compile_features(boolean_world_primitive_bounds_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_primitive_bounds_tests)
    bw_output_dirs(boolean_world_primitive_bounds_tests)
    bw_deploy_runtime_dlls(boolean_world_primitive_bounds_tests)
    add_test(NAME boolean_world_primitive_bounds COMMAND boolean_world_primitive_bounds_tests)

    add_executable(boolean_world_priority_fold_ordering_tests
        tests/priority_fold_ordering_tests.cpp)
    target_link_libraries(boolean_world_priority_fold_ordering_tests PRIVATE core)
    target_compile_features(boolean_world_priority_fold_ordering_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_priority_fold_ordering_tests)
    bw_output_dirs(boolean_world_priority_fold_ordering_tests)
    bw_deploy_runtime_dlls(boolean_world_priority_fold_ordering_tests)
    add_test(NAME boolean_world_priority_fold_ordering COMMAND boolean_world_priority_fold_ordering_tests)

    add_executable(boolean_world_generation_input_snapshot_tests
        tests/generation_input_snapshot_tests.cpp)
    target_link_libraries(boolean_world_generation_input_snapshot_tests PRIVATE core)
    target_compile_features(boolean_world_generation_input_snapshot_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_generation_input_snapshot_tests)
    bw_output_dirs(boolean_world_generation_input_snapshot_tests)
    bw_deploy_runtime_dlls(boolean_world_generation_input_snapshot_tests)
    add_test(NAME boolean_world_generation_input_snapshot COMMAND boolean_world_generation_input_snapshot_tests)

    add_executable(boolean_world_generation_callback_tests
        tests/generation_callback_tests.cpp)
    target_link_libraries(boolean_world_generation_callback_tests PRIVATE core)
    target_compile_features(boolean_world_generation_callback_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_generation_callback_tests)
    bw_output_dirs(boolean_world_generation_callback_tests)
    bw_deploy_runtime_dlls(boolean_world_generation_callback_tests)
    add_test(NAME boolean_world_generation_callbacks COMMAND boolean_world_generation_callback_tests)

    add_executable(boolean_world_generation_commit_queue_tests
        tests/generation_commit_queue_tests.cpp)
    target_link_libraries(boolean_world_generation_commit_queue_tests PRIVATE core)
    target_compile_features(boolean_world_generation_commit_queue_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_generation_commit_queue_tests)
    bw_output_dirs(boolean_world_generation_commit_queue_tests)
    bw_deploy_runtime_dlls(boolean_world_generation_commit_queue_tests)
    add_test(NAME boolean_world_generation_commit_queue COMMAND boolean_world_generation_commit_queue_tests)

    add_executable(boolean_world_exception_regression_tests
        tests/exception_regression_tests.cpp)
    target_link_libraries(boolean_world_exception_regression_tests PRIVATE core)
    target_compile_features(boolean_world_exception_regression_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_exception_regression_tests)
    bw_output_dirs(boolean_world_exception_regression_tests)
    bw_deploy_runtime_dlls(boolean_world_exception_regression_tests)
    add_test(NAME boolean_world_exception_regression COMMAND boolean_world_exception_regression_tests)

    add_executable(boolean_world_serializable_lifetime_tests
        tests/serializable_lifetime_tests.cpp)
    target_link_libraries(boolean_world_serializable_lifetime_tests PRIVATE core)
    target_compile_features(boolean_world_serializable_lifetime_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_serializable_lifetime_tests)
    bw_output_dirs(boolean_world_serializable_lifetime_tests)
    bw_deploy_runtime_dlls(boolean_world_serializable_lifetime_tests)
    add_test(NAME boolean_world_serializable_lifetime COMMAND boolean_world_serializable_lifetime_tests)

    add_executable(boolean_world_serializable_deserialization_tests
        tests/serializable_deserialization_tests.cpp)
    target_link_libraries(boolean_world_serializable_deserialization_tests PRIVATE core)
    target_compile_features(boolean_world_serializable_deserialization_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_serializable_deserialization_tests)
    bw_output_dirs(boolean_world_serializable_deserialization_tests)
    bw_deploy_runtime_dlls(boolean_world_serializable_deserialization_tests)
    add_test(NAME boolean_world_serializable_deserialization COMMAND boolean_world_serializable_deserialization_tests)

    add_executable(boolean_world_trigger_line_layer_selection_tests
        tests/world_trigger_line_layer_selection_tests.cpp)
    target_link_libraries(boolean_world_trigger_line_layer_selection_tests PRIVATE core)
    target_compile_features(boolean_world_trigger_line_layer_selection_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_trigger_line_layer_selection_tests)
    bw_output_dirs(boolean_world_trigger_line_layer_selection_tests)
    bw_deploy_runtime_dlls(boolean_world_trigger_line_layer_selection_tests)
    add_test(NAME boolean_world_trigger_line_layer_selection COMMAND boolean_world_trigger_line_layer_selection_tests)
endif()
