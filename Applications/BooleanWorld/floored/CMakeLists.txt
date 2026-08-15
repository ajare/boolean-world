add_executable(floored WIN32
    src/AppHelpers.cpp
    src/Document.cpp
    src/Expr.cpp
    src/Helpers.cpp
    src/Main.cpp
    src/Render.cpp
    src/UI.cpp
    src/UiHelpers.cpp
    src/WidgetHelpers.cpp
    src/imgui-knobs.cpp
    src/imgui.cpp
    src/imgui_demo.cpp
    src/imgui_draw.cpp
    src/imgui_impl_opengl3.cpp
    src/imgui_impl_sdl3.cpp
    src/imgui_tables.cpp
    src/imgui_widgets.cpp
    src/implot.cpp
    src/implot_items.cpp)

target_include_directories(floored PRIVATE include)

target_compile_definitions(floored PRIVATE _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)

target_compile_definitions(floored PRIVATE YAML_CPP_STATIC_DEFINE USINGZ)

target_link_libraries(floored PRIVATE
    core
    vendor::opengl ext::glew vendor::spdlog vendor::nfd
    ext::yaml-cpp ext::sdl3)

target_compile_features(floored PRIVATE cxx_std_23)
# fmt 11 (bundled in spdlog 1.15) static_asserts that MSVC is compiling as
# UTF-8. All sources here are already valid UTF-8.
target_compile_options(floored PRIVATE /W3 /utf-8)
bw_target_defaults(floored)
bw_output_dirs(floored "bin/x64/$<CONFIG>")
bw_enable_sdl_checks(floored)
bw_no_postfix(floored)
set_target_properties(floored PROPERTIES FOLDER BooleanWorld)

bw_deploy_runtime_dlls(floored)
bw_deploy_vendor_dlls(floored)
bw_deploy_directory(floored "${CMAKE_CURRENT_SOURCE_DIR}/resources")
