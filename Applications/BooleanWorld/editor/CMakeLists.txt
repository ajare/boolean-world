add_executable(editor WIN32
    src/Actions.cpp
    src/AppHelpers.cpp
    src/Document.cpp
    src/Main.cpp
    src/Markdown.cpp
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

target_compile_definitions(editor PRIVATE _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)

target_compile_definitions(editor PRIVATE YAML_CPP_STATIC_DEFINE)

target_link_libraries(editor PRIVATE
    core
    vendor::opengl ext::glew vendor::spdlog vendor::nfd
    ext::yaml-cpp ext::sdl3)

target_compile_features(editor PRIVATE cxx_std_20)
target_compile_options(editor PRIVATE /W3)
bw_target_defaults(editor)
bw_output_dirs(editor "bin/x64/$<CONFIG>")
bw_enable_sdl_checks(editor)
bw_no_postfix(editor)
set_target_properties(editor PROPERTIES FOLDER BooleanWorld)

bw_deploy_runtime_dlls(editor)
bw_deploy_vendor_dlls(editor)
bw_deploy_directory(editor "${CMAKE_CURRENT_SOURCE_DIR}/resources")
# build/support/editor.ini - the editor's saved ImGui layout.
bw_deploy_directory(editor "${CMAKE_CURRENT_SOURCE_DIR}/build/support")
