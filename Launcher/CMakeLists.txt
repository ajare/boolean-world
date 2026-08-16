# Replaces Launcher/build/vs2026/Launcher.sln.
#
# Launcher.exe is a shell: it creates the window and render system, then loads
# a game DLL named in a .cfg file at runtime.

add_executable(Launcher WIN32
    src/ApplicationDLL.cpp
    src/DirectoryResourceLocation.cpp
    src/Main.cpp
    src/ProgramOptions.cpp
    src/StateManager.cpp
    src/Timer.cpp
    src/Window.cpp
    src/ZipResourceLocation.cpp
    src/glfw/ImGuiGLFW.cpp
    src/glfw/TimerGLFW.cpp
    src/glfw/WindowGLFW.cpp
    src/sdl/TimerSDL.cpp
    src/sdl/WindowSDL.cpp
    src/imgui/imgui.cpp
    src/imgui/imgui_demo.cpp
    src/imgui/imgui_draw.cpp
    src/imgui/imgui_tables.cpp
    src/imgui/imgui_widgets.cpp
    src/imgui/implot.cpp
    src/imgui/implot_demo.cpp
    src/imgui/implot_items.cpp)

target_include_directories(Launcher PRIVATE include include/imgui)

target_link_libraries(Launcher PRIVATE
    Willpower.Common Willpower.Application
    ext::mpp ext::Utils
    ext::sdl3 vendor::glfw3)

target_compile_features(Launcher PRIVATE cxx_std_20)
target_compile_options(Launcher PRIVATE /W3)
bw_target_defaults(Launcher)
bw_output_dirs(Launcher "build/vs2026/bin/x64/$<CONFIG>" "build/vs2026/lib/x64/$<CONFIG>")
bw_enable_sdl_checks(Launcher)
bw_add_version_resource(Launcher)
set_target_properties(Launcher PROPERTIES FOLDER Launcher)

if(BUILD_TESTING)
    add_executable(launcher_program_options_tests
        tests/program_options_tests.cpp
        src/ProgramOptions.cpp)
    target_include_directories(launcher_program_options_tests PRIVATE include)
    target_link_libraries(launcher_program_options_tests PRIVATE
        Willpower.Common Willpower.Application ext::Utils)
    target_compile_features(launcher_program_options_tests PRIVATE cxx_std_20)
    bw_target_defaults(launcher_program_options_tests)
    bw_output_dirs(launcher_program_options_tests "build/vs2026/bin/x64/$<CONFIG>")
    bw_deploy_runtime_dlls(launcher_program_options_tests)
    bw_deploy_vendor_dlls(launcher_program_options_tests)
    add_test(NAME launcher_program_options COMMAND launcher_program_options_tests)
endif()

# The game DLL is loaded by name at runtime, so there is no link-time edge -
# state it explicitly so it is always built first.
add_dependencies(Launcher BooleanWorld)

bw_deploy_runtime_dlls(Launcher)
bw_deploy_vendor_dlls(Launcher)

# Per-machine support files, as CopyWillpowerBinaries.bat used to stage:
#   build/support/<COMPUTERNAME>/vs2026/x64/<CONFIG>/*  ->  output directory
# Run through a script so both $<CONFIG> and the directory's existence resolve
# at build time - most machines have no support directory, and
# `cmake -E copy_directory` fails outright on a missing source.
#
# This runs after the generated BooleanWorld.yaml below, so a machine that
# provides its own config overrides the generated one.
if(DEFINED ENV{COMPUTERNAME})
    add_custom_command(TARGET Launcher POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                "-DSRC=${CMAKE_CURRENT_SOURCE_DIR}/build/support/$ENV{COMPUTERNAME}/vs2026/x64/$<CONFIG>"
                "-DDST=$<TARGET_FILE_DIR:Launcher>"
                -P "${BW_ROOT}/cmake/CopyDirIfExists.cmake"
        VERBATIM
        COMMENT "Staging support files for $ENV{COMPUTERNAME}")
endif()

# The game DLL pulls in libraries Launcher itself never links (MppHelper via
# AppLib, for one). Because it is loaded by name rather than linked, those do
# not appear in Launcher's own TARGET_RUNTIME_DLLS - stage the game's set too.
add_custom_command(TARGET Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_RUNTIME_DLLS:BooleanWorld>" "$<TARGET_FILE_DIR:Launcher>"
    COMMAND_EXPAND_LISTS
    VERBATIM
    COMMENT "Staging the game DLL's own dependencies")

# A ready-to-run configuration. Absolute paths, so it does not depend on the
# launcher's working directory or on %COMPUTERNAME% the way the checked-in
# configs under build/support do.
file(GENERATE
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/build/vs2026/bin/x64/$<CONFIG>/BooleanWorld.yaml"
    CONTENT
"Configuration:
  Video:
    Width: 1024
    Height: 768
    Fullscreen: false
    VSync: true
  Audio:
    Enabled: false
    Channels: 1024
    Sync: false
  Game:
    DLL:
      path: $<TARGET_FILE:BooleanWorld>
    ResourceLocations:
      ResourceLocation:
        type: Directory
        path: ${BW_ROOT}/Applications/BooleanWorld/app/resources/
        definition: Resources.yaml
    Debug:
      InGame: enabled
    Arguments:
      Argument:
        name: ThreadedLoading
        value: false
")
