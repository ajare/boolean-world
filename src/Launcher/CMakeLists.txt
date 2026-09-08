# Launcher.exe is a shell: it creates the window and render system, then loads
# a game DLL named in a .cfg file at runtime.

add_executable(Launcher WIN32
    src/ApplicationDLL.cpp
    src/DirectoryResourceLocation.cpp
    src/LauncherLifecycle.cpp
    src/Main.cpp
    src/ProgramOptions.cpp
    src/StateManager.cpp
    src/Timer.cpp
    src/Window.cpp
    src/ZipResourceLocation.cpp
    src/sdl/ImGuiSDL.cpp
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

target_include_directories(Launcher PRIVATE
    include
    include/imgui
    ${BW_ROOT}/src/BooleanWorld/app/include
    ${BW_ROOT}/src/BooleanWorld/render/include)
bw_add_header_filter(Launcher)

target_link_libraries(Launcher PRIVATE
    Willpower.Common Willpower.Application
    ext::mpp ext::Utils
    ext::sdl3
    vendor::opengl)  # [DEBUG-a4f2] glReadPixels for frame capture

target_compile_features(Launcher PRIVATE cxx_std_20)
target_compile_options(Launcher PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W3>)
bw_target_defaults(Launcher)
bw_output_dirs(Launcher)
bw_enable_sdl_checks(Launcher)
set_target_properties(Launcher PROPERTIES
    FOLDER Launcher
    VS_DEBUGGER_ENVIRONMENT "$<$<CONFIG:MemCheck>:${BW_MEMCHECK_ENVIRONMENT}>"
    VS_DEBUGGER_WORKING_DIRECTORY "$<$<CONFIG:Debug,MemCheck>:$(OutDir)>")

if(BUILD_TESTING)
    add_executable(launcher_lifecycle_tests
        tests/launcher_lifecycle_tests.cpp
        src/LauncherLifecycle.cpp)
    target_include_directories(launcher_lifecycle_tests PRIVATE include)
    target_compile_features(launcher_lifecycle_tests PRIVATE cxx_std_20)
    bw_target_defaults(launcher_lifecycle_tests)
    bw_output_dirs(launcher_lifecycle_tests)
    add_test(NAME launcher_lifecycle COMMAND launcher_lifecycle_tests)

    add_executable(launcher_program_options_tests
        tests/program_options_tests.cpp
        src/ProgramOptions.cpp)
    target_include_directories(launcher_program_options_tests PRIVATE
        include
        ${BW_ROOT}/src/BooleanWorld/app/include
        ${BW_ROOT}/src/BooleanWorld/render/include)
    target_link_libraries(launcher_program_options_tests PRIVATE
        Willpower.Common Willpower.Application ext::Utils)
    target_compile_definitions(launcher_program_options_tests PRIVATE
        BW_LAUNCHER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(launcher_program_options_tests PRIVATE cxx_std_20)
    bw_target_defaults(launcher_program_options_tests)
    bw_output_dirs(launcher_program_options_tests)
    bw_deploy_runtime_dlls(launcher_program_options_tests)
    bw_deploy_vendor_dlls(launcher_program_options_tests)
    add_test(NAME launcher_program_options COMMAND launcher_program_options_tests)

    add_executable(launcher_window_input_tests
        tests/window_input_tests.cpp
        src/StateManager.cpp
        src/Window.cpp
        src/sdl/ImGuiSDL.cpp
        src/sdl/WindowSDL.cpp
        src/imgui/imgui.cpp
        src/imgui/imgui_draw.cpp
        src/imgui/imgui_tables.cpp
        src/imgui/imgui_widgets.cpp)
    target_include_directories(launcher_window_input_tests PRIVATE
        include
        include/imgui
        ${BW_ROOT}/src/BooleanWorld/app/include
        ${BW_ROOT}/src/BooleanWorld/render/include)
    target_link_libraries(launcher_window_input_tests PRIVATE
        Willpower.Common Willpower.Application ext::mpp ext::Utils ext::sdl3)
    target_compile_features(launcher_window_input_tests PRIVATE cxx_std_20)
    target_compile_options(launcher_window_input_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W3>)
    bw_target_defaults(launcher_window_input_tests)
    bw_output_dirs(launcher_window_input_tests)
    bw_enable_sdl_checks(launcher_window_input_tests)
    bw_deploy_runtime_dlls(launcher_window_input_tests)
    bw_deploy_vendor_dlls(launcher_window_input_tests)
    add_test(NAME launcher_window_input COMMAND launcher_window_input_tests)

    if(WIN32)
    add_executable(launcher_zip_resource_location_tests
        tests/zip_resource_location_tests.cpp
        src/ZipResourceLocation.cpp)
    target_include_directories(launcher_zip_resource_location_tests PRIVATE include)
    target_link_libraries(launcher_zip_resource_location_tests PRIVATE
        Willpower.Common Willpower.Application ext::Utils)
    target_compile_features(launcher_zip_resource_location_tests PRIVATE cxx_std_20)
    target_compile_options(launcher_zip_resource_location_tests PRIVATE
        /fsanitize=address /Zi)
    target_link_options(launcher_zip_resource_location_tests PRIVATE
        /INFERASANLIBS /DEBUG)
    bw_target_defaults(launcher_zip_resource_location_tests)
    bw_output_dirs(launcher_zip_resource_location_tests)
    bw_deploy_runtime_dlls(launcher_zip_resource_location_tests)
    bw_deploy_vendor_dlls(launcher_zip_resource_location_tests)
    get_filename_component(msvc_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    add_custom_command(TARGET launcher_zip_resource_location_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${msvc_bin_dir}/clang_rt.asan_dynamic-x86_64.dll"
                "$<TARGET_FILE_DIR:launcher_zip_resource_location_tests>")
    add_test(NAME launcher_zip_resource_location
             COMMAND launcher_zip_resource_location_tests)
    endif()
endif()

# The game DLL is loaded by name at runtime, so there is no link-time edge -
# state it explicitly so it is always built first.
add_dependencies(Launcher BooleanWorld)
# BooleanWorld is dlopen'd rather than linked, so explicitly make changes to
# the game library relink Launcher and rerun all of its runtime staging steps.
set_property(TARGET Launcher APPEND PROPERTY
    LINK_DEPENDS "$<TARGET_FILE:BooleanWorld>")

bw_deploy_runtime_dlls(Launcher)
bw_deploy_vendor_dlls(Launcher)

# Shipping is a Windows packaging configuration.
if(WIN32)
add_custom_command(TARGET Launcher POST_BUILD
    COMMAND "$<$<CONFIG:Shipping>:${CMAKE_COMMAND}>" -E copy_if_different
            "$<TARGET_FILE:BooleanWorld>"
            "$<TARGET_FILE_DIR:Launcher>/BooleanWorld.dll"
    COMMAND "$<$<CONFIG:Shipping>:${CMAKE_COMMAND}>" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/Shipping/Game.yaml"
            "$<TARGET_FILE_DIR:Launcher>/Game.yaml"
    COMMAND "$<$<CONFIG:Shipping>:${CMAKE_COMMAND}>" -E copy_directory
            "${BW_ROOT}/src/BooleanWorld/app/resources"
            "$<TARGET_FILE_DIR:Launcher>/resources"
    VERBATIM
    COMMENT "$<$<CONFIG:Shipping>:Staging self-contained Shipping Launcher>")

endif()

if(UNIX AND NOT APPLE)
    # Prefer the copies staged beside Launcher while retaining CMake's build
    # tree paths for targets run from their individual output directories.
    set_property(TARGET Launcher BooleanWorld APPEND PROPERTY BUILD_RPATH
                 "$ORIGIN")

    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/Shipping/Game.yaml"
         _bw_linux_game_config)
    string(REPLACE "BooleanWorld.dll" "./libBooleanWorld.so"
           _bw_linux_game_config "${_bw_linux_game_config}")
    string(REPLACE "path: resources/"
           "path: \"${BW_ROOT}/src/BooleanWorld/app/resources/\""
           _bw_linux_game_config "${_bw_linux_game_config}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/Game-linux.yaml"
         "${_bw_linux_game_config}")
    add_custom_command(TARGET Launcher POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                "-DEXECUTABLE=$<TARGET_FILE:Launcher>"
                "-DGAME_LIBRARY=$<TARGET_FILE:BooleanWorld>"
                "-DDESTINATION=$<TARGET_FILE_DIR:Launcher>"
                "-DPROJECT_ROOT=${BW_ROOT}"
                "-DREADELF=${CMAKE_READELF}"
                -P "${BW_ROOT}/cmake/StageLinuxRuntime.cmake"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_BINARY_DIR}/Game-linux.yaml"
                "$<TARGET_FILE_DIR:Launcher>/Game.yaml"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
                "$<TARGET_FILE_DIR:Launcher>/resources"
        VERBATIM
        COMMENT "Staging Linux Launcher configuration and shared libraries")
    if(BW_ENABLE_FMOD)
        # Steam Audio is loaded through FMOD/dlopen rather than an ELF link
        # edge, so dependency scanning cannot discover these two libraries.
        get_filename_component(_bw_fmod_core_lib_dir
                               "${BW_FMOD_CORE_LIBRARY}" DIRECTORY)
        get_filename_component(_bw_fmod_studio_lib_dir
                               "${BW_FMOD_STUDIO_LIBRARY}" DIRECTORY)
        # This always-out-of-date prerequisite restores the audio runtime even
        # when Launcher itself does not need relinking (and therefore would not
        # execute a POST_BUILD command). The POST_BUILD invocation is still
        # required because StageLinuxRuntime removes old .so files while
        # assembling a freshly linked Launcher's runtime directory.
        add_custom_target(StageLauncherAudio
            COMMAND ${CMAKE_COMMAND}
                    "-DFMOD_CORE_DIR=${_bw_fmod_core_lib_dir}"
                    "-DFMOD_STUDIO_DIR=${_bw_fmod_studio_lib_dir}"
                    "-DSTEAM_AUDIO_LIBRARY=${BW_STEAM_AUDIO_LIBRARY}"
                    "-DSTEAM_AUDIO_FMOD_PLUGIN=${BW_STEAM_AUDIO_FMOD_PLUGIN}"
                    "-DDESTINATION=$<TARGET_FILE_DIR:Launcher>"
                    -P "${BW_ROOT}/cmake/StageLinuxAudio.cmake"
            VERBATIM
            COMMENT "Staging FMOD and Steam Audio for Linux")
        set_property(TARGET StageLauncherAudio PROPERTY FOLDER "Launcher")
        add_dependencies(Launcher StageLauncherAudio)

        add_custom_command(TARGET Launcher POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                    "-DFMOD_CORE_DIR=${_bw_fmod_core_lib_dir}"
                    "-DFMOD_STUDIO_DIR=${_bw_fmod_studio_lib_dir}"
                    "-DSTEAM_AUDIO_LIBRARY=${BW_STEAM_AUDIO_LIBRARY}"
                    "-DSTEAM_AUDIO_FMOD_PLUGIN=${BW_STEAM_AUDIO_FMOD_PLUGIN}"
                    "-DDESTINATION=$<TARGET_FILE_DIR:Launcher>"
                    -P "${BW_ROOT}/cmake/StageLinuxAudio.cmake"
            VERBATIM
            COMMENT "Restaging FMOD and Steam Audio after Linux runtime scan")
        unset(_bw_fmod_core_lib_dir)
        unset(_bw_fmod_studio_lib_dir)
    endif()
endif()

# Per-machine support files:
#   support/<COMPUTERNAME>/<CONFIG>/*  ->  output directory
# Run through a script so both $<CONFIG> and the directory's existence resolve
# at build time - most machines have no support directory, and
# `cmake -E copy_directory` fails outright on a missing source.
if(DEFINED ENV{COMPUTERNAME})
    add_custom_command(TARGET Launcher POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                "-DSRC=${CMAKE_CURRENT_SOURCE_DIR}/support/$ENV{COMPUTERNAME}/$<CONFIG>"
                "-DDST=$<TARGET_FILE_DIR:Launcher>"
                -P "${BW_ROOT}/cmake/CopyDirIfExists.cmake"
        VERBATIM
        COMMENT "Staging support files for $ENV{COMPUTERNAME}")
endif()

# TARGET_RUNTIME_DLLS is a Windows DLL facility. Linux resolves shared objects
# through the generated RPATH and the standalone dependency build directories.
if(WIN32)
    add_custom_command(TARGET Launcher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_RUNTIME_DLLS:BooleanWorld>" "$<TARGET_FILE_DIR:Launcher>"
        COMMAND_EXPAND_LISTS
        VERBATIM
        COMMENT "Staging the game DLL's own dependencies")
endif()
