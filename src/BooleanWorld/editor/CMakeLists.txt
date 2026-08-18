add_executable(editor WIN32
    src/Actions.cpp
    src/AppHelpers.cpp
    src/Document.cpp
    src/Main.cpp
    src/Markdown.cpp
    src/PrimitiveDefaults.cpp
    src/PrimitiveFieldPlacement.cpp
    src/PrimitiveFieldPreview.cpp
    src/Render.cpp
    src/Tiled.cpp
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

target_include_directories(editor PRIVATE include)
bw_add_header_filter(editor)

target_compile_definitions(editor PRIVATE _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)

target_compile_definitions(editor PRIVATE YAML_CPP_STATIC_DEFINE)

target_link_libraries(editor PRIVATE
    core
    vendor::opengl ext::glew vendor::spdlog vendor::nfd
    ext::yaml-cpp ext::sdl3)

target_compile_features(editor PRIVATE cxx_std_20)
# fmt 11 (bundled in spdlog 1.15) static_asserts that MSVC is compiling as
# UTF-8. All sources here are already valid UTF-8.
target_compile_options(editor PRIVATE /W3 /utf-8)
bw_target_defaults(editor)
bw_output_dirs(editor)
bw_enable_sdl_checks(editor)
bw_no_postfix(editor)
set_target_properties(editor PROPERTIES FOLDER BooleanWorld)

bw_deploy_runtime_dlls(editor)
bw_deploy_vendor_dlls(editor)
bw_deploy_directory(editor "${CMAKE_CURRENT_SOURCE_DIR}/resources")
bw_deploy_directory(editor "${CMAKE_CURRENT_SOURCE_DIR}/../core/doc" "doc/core")
bw_deploy_directory(editor "${CMAKE_CURRENT_SOURCE_DIR}/doc" "doc/editor")
# support/editor.ini is the editor's saved ImGui layout.
bw_deploy_directory(editor "${CMAKE_CURRENT_SOURCE_DIR}/support")

if(BUILD_TESTING)
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

    add_executable(boolean_world_editor_primitive_field_placement_tests
        tests/primitive_field_placement_tests.cpp
        src/Document.cpp
        src/PrimitiveDefaults.cpp
        src/PrimitiveFieldPlacement.cpp
        src/PrimitiveFieldPreview.cpp
        src/Undo.cpp)
    target_include_directories(boolean_world_editor_primitive_field_placement_tests PRIVATE include)
    target_compile_definitions(boolean_world_editor_primitive_field_placement_tests PRIVATE
        YAML_CPP_STATIC_DEFINE)
    target_link_libraries(boolean_world_editor_primitive_field_placement_tests PRIVATE
        core vendor::spdlog ext::glew)
    target_compile_features(boolean_world_editor_primitive_field_placement_tests PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_primitive_field_placement_tests PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_primitive_field_placement_tests)
    bw_output_dirs(boolean_world_editor_primitive_field_placement_tests)
    bw_deploy_runtime_dlls(boolean_world_editor_primitive_field_placement_tests)
    add_test(NAME boolean_world_editor_primitive_field_placement
             COMMAND boolean_world_editor_primitive_field_placement_tests)

    add_executable(boolean_world_editor_document_selection_tests
        tests/document_selection_tests.cpp
        src/Document.cpp)
    target_include_directories(boolean_world_editor_document_selection_tests PRIVATE include)
    target_compile_definitions(boolean_world_editor_document_selection_tests PRIVATE
        YAML_CPP_STATIC_DEFINE)
    target_link_libraries(boolean_world_editor_document_selection_tests PRIVATE
        core vendor::spdlog ext::glew)
    target_compile_features(boolean_world_editor_document_selection_tests PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_document_selection_tests PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_document_selection_tests)
    bw_output_dirs(boolean_world_editor_document_selection_tests)
    bw_deploy_runtime_dlls(boolean_world_editor_document_selection_tests)
    add_test(NAME boolean_world_editor_document_selection
             COMMAND boolean_world_editor_document_selection_tests)

    add_executable(boolean_world_editor_undo_history_tests
        tests/undo_history_tests.cpp
        src/Document.cpp
        src/Undo.cpp)
    target_include_directories(boolean_world_editor_undo_history_tests PRIVATE include)
    target_compile_definitions(boolean_world_editor_undo_history_tests PRIVATE
        YAML_CPP_STATIC_DEFINE)
    target_link_libraries(boolean_world_editor_undo_history_tests PRIVATE
        core vendor::spdlog ext::glew)
    target_compile_features(boolean_world_editor_undo_history_tests PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_undo_history_tests PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_undo_history_tests)
    bw_output_dirs(boolean_world_editor_undo_history_tests)
    bw_deploy_runtime_dlls(boolean_world_editor_undo_history_tests)
    add_test(NAME boolean_world_editor_undo_history
             COMMAND boolean_world_editor_undo_history_tests)

    add_executable(boolean_world_editor_prefab_trigger_line_geometry_tests
        tests/prefab_trigger_line_geometry_tests.cpp
        src/Document.cpp)
    target_include_directories(boolean_world_editor_prefab_trigger_line_geometry_tests PRIVATE include)
    target_compile_definitions(boolean_world_editor_prefab_trigger_line_geometry_tests PRIVATE
        YAML_CPP_STATIC_DEFINE)
    target_link_libraries(boolean_world_editor_prefab_trigger_line_geometry_tests PRIVATE
        core vendor::spdlog ext::glew)
    target_compile_features(boolean_world_editor_prefab_trigger_line_geometry_tests PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_prefab_trigger_line_geometry_tests PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_prefab_trigger_line_geometry_tests)
    bw_output_dirs(boolean_world_editor_prefab_trigger_line_geometry_tests)
    bw_deploy_runtime_dlls(boolean_world_editor_prefab_trigger_line_geometry_tests)
    add_test(NAME boolean_world_editor_prefab_trigger_line_geometry
             COMMAND boolean_world_editor_prefab_trigger_line_geometry_tests)

    add_executable(boolean_world_editor_material_colour_tests
        tests/material_colour_tests.cpp
        src/Actions.cpp
        src/Document.cpp
        src/PrimitiveDefaults.cpp)
    target_include_directories(boolean_world_editor_material_colour_tests PRIVATE include)
    target_compile_definitions(boolean_world_editor_material_colour_tests PRIVATE
        YAML_CPP_STATIC_DEFINE)
    target_link_libraries(boolean_world_editor_material_colour_tests PRIVATE
        core vendor::spdlog ext::glew)
    target_compile_features(boolean_world_editor_material_colour_tests PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_material_colour_tests PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_material_colour_tests)
    bw_output_dirs(boolean_world_editor_material_colour_tests)
    bw_deploy_runtime_dlls(boolean_world_editor_material_colour_tests)
    add_test(NAME boolean_world_editor_material_colour
             COMMAND boolean_world_editor_material_colour_tests)
endif()
