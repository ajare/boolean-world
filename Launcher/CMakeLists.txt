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

# USINGZ must match core's Clipper2Z build wherever those headers are seen.
target_compile_definitions(Launcher PRIVATE USINGZ)

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

# The game DLL is loaded by name at runtime, so there is no link-time edge -
# state it explicitly so it is always built first.
add_dependencies(Launcher BooleanWorld)

bw_deploy_runtime_dlls(Launcher)
bw_deploy_vendor_dlls(Launcher)

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
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/build/vs2026/bin/x64/$<CONFIG>/BooleanWorld.cfg"
    CONTENT
"<?xml version=\"1.0\"?>
<Configuration>
\t<Video>
\t\t<Width>1024</Width>
\t\t<Height>768</Height>
\t\t<Fullscreen>false</Fullscreen>
\t\t<VSync>true</VSync>
\t</Video>
\t<Audio>
\t\t<Enabled>false</Enabled>
\t\t<Channels>1024</Channels>
\t\t<Sync>false</Sync>
\t</Audio>
\t<Game>
\t\t<DLL path=\"$<TARGET_FILE:BooleanWorld>\" />
\t\t<GameResource>BooleanWorld</GameResource>
\t\t<ResourceLocations>
\t\t\t<ResourceLocation type=\"Directory\" path=\"${BW_ROOT}/Applications/BooleanWorld/app/resources/\" definition=\"Resources.xml\" />
\t\t</ResourceLocations>
\t\t<Debug>
\t\t\t<InGame>enabled</InGame>
\t\t</Debug>
\t\t<Arguments>
\t\t\t<Argument name=\"ThreadedLoading\">false</Argument>
\t\t</Arguments>
\t</Game>
</Configuration>
")
