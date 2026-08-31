add_executable(editor WIN32
    src/Actions.cpp
    src/ApplicationCloseController.cpp
    src/AppHelpers.cpp
    src/Document.cpp
    src/EditorRenderSystem.cpp
    src/EmbossingCatalogLibrary.cpp
    src/Interaction.cpp
    src/Main.cpp
    src/Markdown.cpp
    src/PrimitiveDefaults.cpp
    src/PrimitiveFieldPlacement.cpp
    src/PrimitiveFieldPreview.cpp
    src/ProcMaterialLibrary.cpp
    src/SubMaterialThumbnailRenderer.cpp
    src/PrefabPlacementPreview.cpp
    src/PrefabTilingGuide.cpp
    src/Preview3D.cpp
    src/PreviewOutlineRenderer.cpp
    src/PreviewRenderScene.cpp
    src/PreviewSurfaceOutline.cpp
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
    BW_EDITOR_PROC_MATERIAL_MANIFEST="${CMAKE_CURRENT_SOURCE_DIR}/../app/resources/Resources.yaml"
    BW_EDITOR_RESOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../app/resources")

target_compile_definitions(editor PRIVATE YAML_CPP_STATIC_DEFINE)

target_link_libraries(editor PRIVATE
    core Willpower.Geometry
    BooleanWorldRender
    vendor::opengl ext::glew
    # mpp/mpp-data/mpp-helper/mpp-mesh/mpp-program/Utils are also pulled in
    # transitively via BooleanWorldRender's public link requirements.
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
bw_deploy_directory(editor "${CMAKE_CURRENT_SOURCE_DIR}/../app/resources/images" "images")
bw_deploy_directory(editor "${CMAKE_CURRENT_SOURCE_DIR}/../core/doc" "doc/core")
bw_deploy_directory(editor "${CMAKE_CURRENT_SOURCE_DIR}/doc" "doc/editor")

if(BUILD_TESTING)
    add_executable(boolean_world_editor_preview_surface_pick_tests
        tests/preview_surface_pick_tests.cpp
        src/PreviewSurfaceOutline.cpp
        src/PreviewSurfacePick.cpp)
    target_include_directories(boolean_world_editor_preview_surface_pick_tests PRIVATE include)
    target_compile_definitions(boolean_world_editor_preview_surface_pick_tests PRIVATE
        YAML_CPP_STATIC_DEFINE)
    target_link_libraries(boolean_world_editor_preview_surface_pick_tests PRIVATE
        core)
    target_compile_features(boolean_world_editor_preview_surface_pick_tests PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_preview_surface_pick_tests PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_preview_surface_pick_tests)
    bw_output_dirs(boolean_world_editor_preview_surface_pick_tests)
    bw_deploy_runtime_dlls(boolean_world_editor_preview_surface_pick_tests)
    add_test(NAME boolean_world_editor_preview_surface_pick
             COMMAND boolean_world_editor_preview_surface_pick_tests)

    # Not registered via add_test - needs a real GPU/driver. This replaces the
    # deleted PreviewMaterialProgram smoke with the WorldRenderer path now used
    # by the editor preview.
    add_executable(boolean_world_editor_world_renderer_smoke
        tests/world_renderer_smoke.cpp
        src/EditorRenderSystem.cpp
        src/PreviewOutlineRenderer.cpp
        src/PreviewRenderScene.cpp
        ../app/src/ReactiveCamera.cpp)
    target_include_directories(boolean_world_editor_world_renderer_smoke PRIVATE
        include ../app/include)
    target_compile_definitions(boolean_world_editor_world_renderer_smoke PRIVATE
        YAML_CPP_STATIC_DEFINE
        BW_EDITOR_PROC_MATERIAL_MANIFEST="${CMAKE_CURRENT_SOURCE_DIR}/../app/resources/Resources.yaml")
    target_link_libraries(boolean_world_editor_world_renderer_smoke PRIVATE
        core BooleanWorldRender Willpower.Application Willpower.Common
        Willpower.Geometry vendor::opengl ext::glew
        ext::mpp ext::mpp-helper ext::mpp-mesh ext::Utils ext::sdl3
        ext::yaml-cpp)
    target_compile_features(boolean_world_editor_world_renderer_smoke PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_world_renderer_smoke PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_world_renderer_smoke)
    bw_output_dirs(boolean_world_editor_world_renderer_smoke)
    bw_deploy_runtime_dlls(boolean_world_editor_world_renderer_smoke)
    bw_deploy_vendor_dlls(boolean_world_editor_world_renderer_smoke)

    # Not registered via add_test - needs a real GPU/driver. Build and run it
    # manually to see EditorRenderSystem's bootstrap/teardown work (or fail)
    # directly rather than only discovering it when the 3D preview is wired
    # into it in a later ticket.
    add_executable(boolean_world_editor_render_system_smoke
        tests/editor_render_system_smoke.cpp
        src/EditorRenderSystem.cpp)
    target_include_directories(boolean_world_editor_render_system_smoke PRIVATE include)
    target_compile_definitions(boolean_world_editor_render_system_smoke PRIVATE
        YAML_CPP_STATIC_DEFINE
        BW_EDITOR_PROC_MATERIAL_MANIFEST="${CMAKE_CURRENT_SOURCE_DIR}/../app/resources/Resources.yaml")
    target_link_libraries(boolean_world_editor_render_system_smoke PRIVATE
        core BooleanWorldRender Willpower.Application Willpower.Common
        vendor::opengl ext::glew
        ext::mpp ext::mpp-helper ext::mpp-mesh ext::Utils ext::sdl3
        ext::yaml-cpp)
    target_compile_features(boolean_world_editor_render_system_smoke PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_render_system_smoke PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_render_system_smoke)
    bw_output_dirs(boolean_world_editor_render_system_smoke)
    bw_deploy_runtime_dlls(boolean_world_editor_render_system_smoke)
    bw_deploy_vendor_dlls(boolean_world_editor_render_system_smoke)

    # Not registered via add_test - needs a real GPU/driver. Drives the whole
    # 3D preview render stack headlessly, including the repeated open/close
    # cycle GitHub issue #270 asks to be free of GPU resource leaks.
    add_executable(boolean_world_editor_preview_render_stack_smoke
        tests/preview_render_stack_smoke.cpp
        src/EditorRenderSystem.cpp
        src/PreviewOutlineRenderer.cpp
        src/PreviewRenderScene.cpp
        src/ProcMaterialLibrary.cpp
        src/SubMaterialThumbnailRenderer.cpp
        ../app/src/ReactiveCamera.cpp)
    target_include_directories(boolean_world_editor_preview_render_stack_smoke PRIVATE
        include ../app/include)
    target_compile_definitions(boolean_world_editor_preview_render_stack_smoke PRIVATE
        YAML_CPP_STATIC_DEFINE
        BW_EDITOR_PROC_MATERIAL_MANIFEST="${CMAKE_CURRENT_SOURCE_DIR}/../app/resources/Resources.yaml"
        BW_EDITOR_PREVIEW_TEST_WORLD="${CMAKE_CURRENT_SOURCE_DIR}/../app/resources/world-test-1.world.yaml")
    target_link_libraries(boolean_world_editor_preview_render_stack_smoke PRIVATE
        core BooleanWorldRender Willpower.Application Willpower.Common
        Willpower.Geometry vendor::opengl ext::glew
        ext::mpp ext::mpp-helper ext::mpp-mesh ext::Utils ext::sdl3
        ext::yaml-cpp)
    target_compile_features(boolean_world_editor_preview_render_stack_smoke PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_preview_render_stack_smoke PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_preview_render_stack_smoke)
    bw_output_dirs(boolean_world_editor_preview_render_stack_smoke)
    bw_deploy_runtime_dlls(boolean_world_editor_preview_render_stack_smoke)
    bw_deploy_vendor_dlls(boolean_world_editor_preview_render_stack_smoke)

    # Not registered via add_test - renders every catalog Sub-material through
    # the real world material shaders and therefore needs a real GPU/driver.
    add_executable(boolean_world_editor_sub_material_thumbnail_smoke
        tests/sub_material_thumbnail_smoke.cpp
        src/EditorRenderSystem.cpp
        src/PreviewOutlineRenderer.cpp
        src/PreviewRenderScene.cpp
        src/ProcMaterialLibrary.cpp
        src/SubMaterialThumbnailRenderer.cpp)
    target_include_directories(boolean_world_editor_sub_material_thumbnail_smoke PRIVATE
        include ../app/include)
    target_compile_definitions(boolean_world_editor_sub_material_thumbnail_smoke PRIVATE
        YAML_CPP_STATIC_DEFINE
        BW_EDITOR_PROC_MATERIAL_MANIFEST="${CMAKE_CURRENT_SOURCE_DIR}/../app/resources/Resources.yaml")
    target_link_libraries(boolean_world_editor_sub_material_thumbnail_smoke PRIVATE
        core BooleanWorldRender Willpower.Application Willpower.Common
        Willpower.Geometry vendor::opengl ext::glew
        ext::mpp ext::mpp-helper ext::mpp-mesh ext::Utils ext::sdl3
        ext::yaml-cpp)
    target_compile_features(boolean_world_editor_sub_material_thumbnail_smoke PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_sub_material_thumbnail_smoke PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_sub_material_thumbnail_smoke)
    bw_output_dirs(boolean_world_editor_sub_material_thumbnail_smoke)
    bw_deploy_runtime_dlls(boolean_world_editor_sub_material_thumbnail_smoke)
    bw_deploy_vendor_dlls(boolean_world_editor_sub_material_thumbnail_smoke)

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
        src/PrefabPlacementPreview.cpp
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

    add_executable(boolean_world_editor_application_close_controller_tests
        tests/application_close_controller_tests.cpp
        src/ApplicationCloseController.cpp)
    target_include_directories(boolean_world_editor_application_close_controller_tests PRIVATE include)
    target_compile_features(boolean_world_editor_application_close_controller_tests PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_application_close_controller_tests PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_application_close_controller_tests)
    bw_output_dirs(boolean_world_editor_application_close_controller_tests)
    add_test(NAME boolean_world_editor_application_close_controller
             COMMAND boolean_world_editor_application_close_controller_tests)

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
        YAML_CPP_STATIC_DEFINE
        BW_EDITOR_RESOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../app/resources")
    target_link_libraries(boolean_world_editor_interaction_tests PRIVATE
        core BooleanWorldRender vendor::spdlog ext::glew ext::yaml-cpp)
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

    add_executable(boolean_world_editor_primitive_surface_z_tests
        tests/primitive_surface_z_tests.cpp
        src/Actions.cpp
        src/Document.cpp
        src/PrimitiveDefaults.cpp
        src/ProcMaterialLibrary.cpp
        src/Undo.cpp)
    target_include_directories(boolean_world_editor_primitive_surface_z_tests PRIVATE include)
    target_compile_definitions(boolean_world_editor_primitive_surface_z_tests PRIVATE
        YAML_CPP_STATIC_DEFINE)
    target_link_libraries(boolean_world_editor_primitive_surface_z_tests PRIVATE
        core vendor::spdlog ext::glew ext::yaml-cpp)
    target_compile_features(boolean_world_editor_primitive_surface_z_tests PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_primitive_surface_z_tests PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_primitive_surface_z_tests)
    bw_output_dirs(boolean_world_editor_primitive_surface_z_tests)
    bw_deploy_runtime_dlls(boolean_world_editor_primitive_surface_z_tests)
    add_test(NAME boolean_world_editor_primitive_surface_z
             COMMAND boolean_world_editor_primitive_surface_z_tests)

    add_executable(boolean_world_editor_sub_material_picker_tests
        tests/sub_material_picker_tests.cpp
        src/Actions.cpp
        src/Document.cpp
        src/PrimitiveDefaults.cpp
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

    add_executable(boolean_world_editor_embossing_catalog_authoring_tests
        tests/embossing_catalog_authoring_tests.cpp
        src/Actions.cpp
        src/Document.cpp
        src/EmbossingCatalogLibrary.cpp
        src/PrimitiveDefaults.cpp
        src/ProcMaterialLibrary.cpp
        src/Undo.cpp)
    target_include_directories(boolean_world_editor_embossing_catalog_authoring_tests PRIVATE include)
    target_compile_definitions(boolean_world_editor_embossing_catalog_authoring_tests PRIVATE
        YAML_CPP_STATIC_DEFINE)
    target_link_libraries(boolean_world_editor_embossing_catalog_authoring_tests PRIVATE
        core vendor::spdlog ext::glew ext::yaml-cpp BooleanWorldRender)
    target_compile_features(boolean_world_editor_embossing_catalog_authoring_tests PRIVATE cxx_std_20)
    target_compile_options(boolean_world_editor_embossing_catalog_authoring_tests PRIVATE /W3 /utf-8)
    bw_target_defaults(boolean_world_editor_embossing_catalog_authoring_tests)
    bw_output_dirs(boolean_world_editor_embossing_catalog_authoring_tests)
    bw_deploy_runtime_dlls(boolean_world_editor_embossing_catalog_authoring_tests)
    add_test(NAME boolean_world_editor_embossing_catalog_authoring
             COMMAND boolean_world_editor_embossing_catalog_authoring_tests)

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

    # Undo snapshots include both directly-authored resource libraries. Every
    # lightweight action/Document test above compiles Undo.cpp, so provide the
    # global Embossing-catalog implementation to each one as well.
    foreach(target
            boolean_world_editor_primitive_field_placement_tests
            boolean_world_editor_interaction_tests
            boolean_world_editor_document_selection_tests
            boolean_world_editor_document_layer_import_export_tests
            boolean_world_editor_undo_history_tests
            boolean_world_editor_primitive_surface_z_tests
            boolean_world_editor_sub_material_picker_tests
            boolean_world_editor_layer_build_step_actions_tests)
        target_sources(${target} PRIVATE src/EmbossingCatalogLibrary.cpp)
    endforeach()

    # Document validates candidate Worlds through the shared render-side image
    # decoder, so every lightweight Document test needs that implementation.
    foreach(target
            boolean_world_editor_primitive_field_placement_tests
            boolean_world_editor_interaction_tests
            boolean_world_editor_document_selection_tests
            boolean_world_editor_document_layer_import_export_tests
            boolean_world_editor_undo_history_tests
            boolean_world_editor_primitive_surface_z_tests
            boolean_world_editor_sub_material_picker_tests
            boolean_world_editor_layer_build_step_actions_tests)
        target_link_libraries(${target} PRIVATE BooleanWorldRender)
    endforeach()
endif()
