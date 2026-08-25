add_executable(editor WIN32
    src/Actions.cpp
    src/AppHelpers.cpp
    src/Document.cpp
    src/Interaction.cpp
    src/Main.cpp
    src/Markdown.cpp
    src/PrimitiveDefaults.cpp
    src/PrimitiveFieldPlacement.cpp
    src/PrimitiveFieldPreview.cpp
    src/PrimitivePreviewGeometry.cpp
    src/ProcMaterialLibrary.cpp
    src/PrefabTilingGuide.cpp
    src/Preview3D.cpp
    src/PreviewMaterialProgram.cpp
    src/PreviewSurfacePick.cpp
    ../app/src/ReactiveCamera.cpp
    src/Render.cpp
    src/UI.cpp
    src/UiHelpers.cpp
    src/Undo.cpp
    src/WidgetHelpers.cpp
    src/imgui-knobs.cpp
    src/imgui.cpp
    src/imgui_demo.cpp
    src/imgui_draw.cpp
    src/imgui_impl_opengl3.cpp
    src/imgui_impl_sdl3.cpp
    src/imgui_tables.cpp
    src/imgui_widgets.cpp
    src/imnodes.cpp
    src/implot.cpp
    src/implot_items.cpp)

target_include_directories(editor PRIVATE include ../app/include)
bw_add_header_filter(editor)

target_compile_definitions(editor PRIVATE
    _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING
    BW_EDITOR_PROC_MATERIAL_MANIFEST="${CMAKE_CURRENT_SOURCE_DIR}/../app/resources/Resources.yaml")

target_compile_definitions(editor PRIVATE YAML_CPP_STATIC_DEFINE)

target_link_libraries(editor PRIVATE
    core Willpower.Geometry
    vendor::opengl ext::glew
    ext::mpp ext::mpp-data ext::mpp-helper ext::mpp-mesh ext::mpp-program ext::Utils
    vendor::spdlog vendor::nfd
    ext::yaml-cpp ext::sdl3)

target_compile_features(editor PRIVATE cxx_std_20)
# fmt 11 (bundled in spdlog 1.15) static_asserts that MSVC is compiling as
# UTF-8. All sources here are already valid UTF-8.
target_compile_options(editor PRIVATE /W3 /utf-8)
bw_target_defaults(editor)
bw_output_dirs(editor)
bw_enable_sdl_checks(editor)
bw_no_postfix(editor)
set_target_properties(editor PROPERTIES
    FOLDER BooleanWorld
    VS_DEBUGGER_WORKING_DIRECTORY "$(OutDir)")

bw_deploy_runtime_dlls(editor)
bw_deploy_vendor_dlls(editor)
bw_deploy_directory(editor "${CMAKE_CURRENT_SOURCE_DIR}/resources")
# The 3D preview compiles the game's own material shaders, so it stages them
# from the app module rather than keeping a second copy that could drift.
bw_deploy_directory(editor "${CMAKE_CURRENT_SOURCE_DIR}/../app/resources/shaders" "shaders")
bw_deploy_directory(editor "${CMAKE_CURRENT_SOURCE_DIR}/../core/doc" "doc/core")
bw_deploy_directory(editor "${CMAKE_CURRENT_SOURCE_DIR}/doc" "doc/editor")

if(BUILD_TESTING)
    add_executable(boolean_world_editor_primitive_preview_geometry_tests
        tests/primitive_preview_geometry_tests.cpp
        src/PrimitivePreviewGeometry.cpp
        src/ProcMaterialLibrary.cpp)
    target_include_directories(boolean_world_editor_primitive_preview_geometry_tests PRIVATE include)
    target_compile_definitions(boolean_world_editor_primitive_preview_geometry_tests PRIVATE
        YAML_CPP_STATIC_DEFINE)
    target_link_libraries(boolean_world_editor_primitive_preview_geometry_tests PRIVATE
        core ext::yaml-cpp)
    target_compile_features(boolean_world_editor_primitive_preview_geometry_tests PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_primitive_preview_geometry_tests PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_primitive_preview_geometry_tests)
    bw_output_dirs(boolean_world_editor_primitive_preview_geometry_tests)
    bw_deploy_runtime_dlls(boolean_world_editor_primitive_preview_geometry_tests)
    add_test(NAME boolean_world_editor_primitive_preview_geometry
             COMMAND boolean_world_editor_primitive_preview_geometry_tests)

    add_executable(boolean_world_editor_preview_surface_pick_tests
        tests/preview_surface_pick_tests.cpp
        src/PreviewSurfacePick.cpp
        src/PrimitivePreviewGeometry.cpp
        src/ProcMaterialLibrary.cpp)
    target_include_directories(boolean_world_editor_preview_surface_pick_tests PRIVATE include)
    target_compile_definitions(boolean_world_editor_preview_surface_pick_tests PRIVATE
        YAML_CPP_STATIC_DEFINE)
    target_link_libraries(boolean_world_editor_preview_surface_pick_tests PRIVATE
        core ext::yaml-cpp)
    target_compile_features(boolean_world_editor_preview_surface_pick_tests PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_preview_surface_pick_tests PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_preview_surface_pick_tests)
    bw_output_dirs(boolean_world_editor_preview_surface_pick_tests)
    bw_deploy_runtime_dlls(boolean_world_editor_preview_surface_pick_tests)
    add_test(NAME boolean_world_editor_preview_surface_pick
             COMMAND boolean_world_editor_preview_surface_pick_tests)

    # Not registered via add_test - needs a real GPU/driver. Build and run it
    # manually to see PreviewMaterialProgram's shader compile/link errors
    # directly instead of a silently black 3D preview window.
    add_executable(boolean_world_editor_preview_material_program_smoke
        tests/preview_material_program_smoke.cpp
        src/PreviewMaterialProgram.cpp)
    target_include_directories(boolean_world_editor_preview_material_program_smoke PRIVATE include)
    target_link_libraries(boolean_world_editor_preview_material_program_smoke PRIVATE
        core vendor::opengl ext::glew
        ext::mpp ext::mpp-data ext::mpp-helper ext::mpp-mesh ext::mpp-program ext::Utils
        vendor::spdlog ext::sdl3)
    target_compile_features(boolean_world_editor_preview_material_program_smoke PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_preview_material_program_smoke PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_preview_material_program_smoke)
    bw_output_dirs(boolean_world_editor_preview_material_program_smoke)
    bw_deploy_runtime_dlls(boolean_world_editor_preview_material_program_smoke)
    bw_deploy_vendor_dlls(boolean_world_editor_preview_material_program_smoke)
    bw_deploy_directory(boolean_world_editor_preview_material_program_smoke
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/shaders" "shaders")
    bw_deploy_directory(boolean_world_editor_preview_material_program_smoke
        "${CMAKE_CURRENT_SOURCE_DIR}/../app/resources/shaders" "shaders")

    add_executable(boolean_world_editor_primitive_field_preview_tests
        tests/primitive_field_preview_tests.cpp
        src/PrimitiveFieldPreview.cpp)
    target_include_directories(boolean_world_editor_primitive_field_preview_tests PRIVATE include)
    target_link_libraries(boolean_world_editor_primitive_field_preview_tests PRIVATE core)
    target_compile_features(boolean_world_editor_primitive_field_preview_tests PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_primitive_field_preview_tests PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_primitive_field_preview_tests)
    bw_output_dirs(boolean_world_editor_primitive_field_preview_tests)
    bw_deploy_runtime_dlls(boolean_world_editor_primitive_field_preview_tests)
    add_test(NAME boolean_world_editor_primitive_field_preview
             COMMAND boolean_world_editor_primitive_field_preview_tests)

    add_executable(boolean_world_editor_prefab_tiling_guide_tests
        tests/prefab_tiling_guide_tests.cpp
        src/PrefabTilingGuide.cpp)
    target_include_directories(boolean_world_editor_prefab_tiling_guide_tests PRIVATE include)
    target_link_libraries(boolean_world_editor_prefab_tiling_guide_tests PRIVATE core)
    target_compile_features(boolean_world_editor_prefab_tiling_guide_tests PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_prefab_tiling_guide_tests PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_prefab_tiling_guide_tests)
    bw_output_dirs(boolean_world_editor_prefab_tiling_guide_tests)
    bw_deploy_runtime_dlls(boolean_world_editor_prefab_tiling_guide_tests)
    add_test(NAME boolean_world_editor_prefab_tiling_guide
             COMMAND boolean_world_editor_prefab_tiling_guide_tests)

    add_executable(boolean_world_editor_primitive_field_placement_tests
        tests/primitive_field_placement_tests.cpp
        src/Document.cpp
        src/PrimitiveDefaults.cpp
        src/PrimitiveFieldPlacement.cpp
        src/PrimitiveFieldPreview.cpp
        src/Undo.cpp
        src/ProcMaterialLibrary.cpp)
    target_include_directories(boolean_world_editor_primitive_field_placement_tests PRIVATE include)
    target_compile_definitions(boolean_world_editor_primitive_field_placement_tests PRIVATE
        YAML_CPP_STATIC_DEFINE)
    target_link_libraries(boolean_world_editor_primitive_field_placement_tests PRIVATE
        core vendor::spdlog ext::glew ext::yaml-cpp)
    target_compile_features(boolean_world_editor_primitive_field_placement_tests PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_primitive_field_placement_tests PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_primitive_field_placement_tests)
    bw_output_dirs(boolean_world_editor_primitive_field_placement_tests)
    bw_deploy_runtime_dlls(boolean_world_editor_primitive_field_placement_tests)
    add_test(NAME boolean_world_editor_primitive_field_placement
             COMMAND boolean_world_editor_primitive_field_placement_tests)

    add_executable(boolean_world_editor_document_selection_tests
        tests/document_selection_tests.cpp
        src/Document.cpp
        src/Undo.cpp
        src/ProcMaterialLibrary.cpp)
    target_include_directories(boolean_world_editor_document_selection_tests PRIVATE include)
    target_compile_definitions(boolean_world_editor_document_selection_tests PRIVATE
        YAML_CPP_STATIC_DEFINE)
    target_link_libraries(boolean_world_editor_document_selection_tests PRIVATE
        core vendor::spdlog ext::glew ext::yaml-cpp)
    target_compile_features(boolean_world_editor_document_selection_tests PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_document_selection_tests PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_document_selection_tests)
    bw_output_dirs(boolean_world_editor_document_selection_tests)
    bw_deploy_runtime_dlls(boolean_world_editor_document_selection_tests)
    add_test(NAME boolean_world_editor_document_selection
             COMMAND boolean_world_editor_document_selection_tests)

    add_executable(boolean_world_editor_interaction_tests
        tests/editor_interaction_tests.cpp
        src/Actions.cpp
        src/Document.cpp
        src/Interaction.cpp
        src/PrimitiveDefaults.cpp
        src/Undo.cpp
        src/ProcMaterialLibrary.cpp)
    target_include_directories(boolean_world_editor_interaction_tests PRIVATE include)
    target_compile_definitions(boolean_world_editor_interaction_tests PRIVATE
        YAML_CPP_STATIC_DEFINE)
    target_link_libraries(boolean_world_editor_interaction_tests PRIVATE
        core vendor::spdlog ext::glew ext::yaml-cpp)
    target_compile_features(boolean_world_editor_interaction_tests PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_interaction_tests PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_interaction_tests)
    bw_output_dirs(boolean_world_editor_interaction_tests)
    bw_deploy_runtime_dlls(boolean_world_editor_interaction_tests)
    add_test(NAME boolean_world_editor_interaction
             COMMAND boolean_world_editor_interaction_tests)

    add_executable(boolean_world_editor_document_layer_import_export_tests
        tests/document_layer_import_export_tests.cpp
        src/Document.cpp
        src/Undo.cpp
        src/ProcMaterialLibrary.cpp)
    target_include_directories(boolean_world_editor_document_layer_import_export_tests PRIVATE include)
    target_compile_definitions(boolean_world_editor_document_layer_import_export_tests PRIVATE
        YAML_CPP_STATIC_DEFINE)
    target_link_libraries(boolean_world_editor_document_layer_import_export_tests PRIVATE
        core vendor::spdlog ext::glew ext::yaml-cpp)
    target_compile_features(boolean_world_editor_document_layer_import_export_tests PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_document_layer_import_export_tests PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_document_layer_import_export_tests)
    bw_output_dirs(boolean_world_editor_document_layer_import_export_tests)
    bw_deploy_runtime_dlls(boolean_world_editor_document_layer_import_export_tests)
    add_test(NAME boolean_world_editor_document_layer_import_export
             COMMAND boolean_world_editor_document_layer_import_export_tests)

    add_executable(boolean_world_editor_undo_history_tests
        tests/undo_history_tests.cpp
        src/Actions.cpp
        src/Document.cpp
        src/PrimitiveDefaults.cpp
        src/Undo.cpp
        src/ProcMaterialLibrary.cpp)
    target_include_directories(boolean_world_editor_undo_history_tests PRIVATE include)
    target_compile_definitions(boolean_world_editor_undo_history_tests PRIVATE
        YAML_CPP_STATIC_DEFINE)
    target_link_libraries(boolean_world_editor_undo_history_tests PRIVATE
        core vendor::spdlog ext::glew ext::yaml-cpp)
    target_compile_features(boolean_world_editor_undo_history_tests PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_undo_history_tests PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_undo_history_tests)
    bw_output_dirs(boolean_world_editor_undo_history_tests)
    bw_deploy_runtime_dlls(boolean_world_editor_undo_history_tests)
    add_test(NAME boolean_world_editor_undo_history
             COMMAND boolean_world_editor_undo_history_tests)

    add_executable(boolean_world_editor_sub_material_picker_tests
        tests/sub_material_picker_tests.cpp
        src/Actions.cpp
        src/Document.cpp
        src/PrimitiveDefaults.cpp
        src/PrimitivePreviewGeometry.cpp
        src/ProcMaterialLibrary.cpp
        src/Undo.cpp)
    target_include_directories(boolean_world_editor_sub_material_picker_tests PRIVATE include)
    target_compile_definitions(boolean_world_editor_sub_material_picker_tests PRIVATE
        YAML_CPP_STATIC_DEFINE)
    target_link_libraries(boolean_world_editor_sub_material_picker_tests PRIVATE
        core vendor::spdlog ext::glew ext::yaml-cpp)
    target_compile_features(boolean_world_editor_sub_material_picker_tests PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_sub_material_picker_tests PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_sub_material_picker_tests)
    bw_output_dirs(boolean_world_editor_sub_material_picker_tests)
    bw_deploy_runtime_dlls(boolean_world_editor_sub_material_picker_tests)
    add_test(NAME boolean_world_editor_sub_material_picker
             COMMAND boolean_world_editor_sub_material_picker_tests)

    add_executable(boolean_world_editor_layer_build_step_actions_tests
        tests/layer_build_step_actions_tests.cpp
        src/Actions.cpp
        src/Document.cpp
        src/PrimitiveDefaults.cpp
        src/Undo.cpp
        src/ProcMaterialLibrary.cpp)
    target_include_directories(boolean_world_editor_layer_build_step_actions_tests PRIVATE include)
    target_compile_definitions(boolean_world_editor_layer_build_step_actions_tests PRIVATE
        YAML_CPP_STATIC_DEFINE)
    target_link_libraries(boolean_world_editor_layer_build_step_actions_tests PRIVATE
        core vendor::spdlog ext::glew ext::yaml-cpp)
    target_compile_features(boolean_world_editor_layer_build_step_actions_tests PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_layer_build_step_actions_tests PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_layer_build_step_actions_tests)
    bw_output_dirs(boolean_world_editor_layer_build_step_actions_tests)
    bw_deploy_runtime_dlls(boolean_world_editor_layer_build_step_actions_tests)
    add_test(NAME boolean_world_editor_layer_build_step_actions
             COMMAND boolean_world_editor_layer_build_step_actions_tests)
endif()
