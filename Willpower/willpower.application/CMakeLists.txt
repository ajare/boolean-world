# Adopted wholesale from tungsten-oxide (cpp/willpower/willpower.application).
#
# The substantive change is the resource system: every definition factory now
# takes a wp::DataNode* instead of a utils::XmlNode*, so resource definitions
# can be read from YAML. DirectoryResourceLocation moved here from Launcher.

add_library(Willpower.Application SHARED
    src/ApplicationSettings.cpp
    src/AudioSystem.cpp
    src/InputHelper.cpp
    src/InputStateManager.cpp
    src/Scheduler.cpp
    src/SchedulerTask.cpp
    src/ServiceLocator.cpp
    src/State.cpp
    src/StateFactory.cpp
    src/resourcesystem/AnimationSetDefaultDefinitionFactory.cpp
    src/resourcesystem/AnimationSetResource.cpp
    src/resourcesystem/AnimationSetResourceDefinitionFactory.cpp
    src/resourcesystem/AudioBankDefaultDefinitionFactory.cpp
    src/resourcesystem/AudioBankResource.cpp
    src/resourcesystem/AudioBankResourceDefinitionFactory.cpp
    src/resourcesystem/DataStream.cpp
    src/resourcesystem/DirectoryResourceLocation.cpp
    src/resourcesystem/ImageDefaultDefinitionFactory.cpp
    src/resourcesystem/ImageResource.cpp
    src/resourcesystem/ImageResourceDefinitionFactory.cpp
    src/resourcesystem/ImageSetDefaultDefinitionFactory.cpp
    src/resourcesystem/ImageSetResource.cpp
    src/resourcesystem/ImageSetResourceDefinitionFactory.cpp
    src/resourcesystem/MaterialResource.cpp
    src/resourcesystem/MaterialResourceDefaultDefinitionFactory.cpp
    src/resourcesystem/MaterialResourceDefinitionFactory.cpp
    src/resourcesystem/ProgramDefaultDefinitionFactory.cpp
    src/resourcesystem/ProgramResource.cpp
    src/resourcesystem/ProgramResourceDefinitionFactory.cpp
    src/resourcesystem/Resource.cpp
    src/resourcesystem/ResourceDefinitionFactory.cpp
    src/resourcesystem/ResourceLocation.cpp
    src/resourcesystem/ResourceManager.cpp
    src/resourcesystem/ShaderDefaultDefinitionFactory.cpp
    src/resourcesystem/ShaderResource.cpp
    src/resourcesystem/ShaderResourceDefinitionFactory.cpp
    # Defines TextFileDefaultDefinitionFactory - the file name is a typo.
    src/resourcesystem/TestFileDefaultDefinitionFactory.cpp
    src/resourcesystem/TextFileResource.cpp
    src/resourcesystem/TextFileResourceDefinitionFactory.cpp
    src/resourcesystem/XmlFileDefaultDefinitionFactory.cpp
    src/resourcesystem/XmlFileResource.cpp
    src/resourcesystem/XmlFileResourceDefinitionFactory.cpp)

target_include_directories(Willpower.Application PUBLIC include)

target_compile_definitions(Willpower.Application
    PUBLIC  WP_APPLICATION_USE_FMOD
    PRIVATE WIN32 _WINDOWS
            WP_APPLICATION_DLL_EXPORT
            WP_USE_ASSERT_TRACE
            $<$<CONFIG:Debug>:WP_USE_MEMLEAK_TRACKING>)

# No longer links Willpower.Collide or Willpower.Geometry - tungsten-oxide's
# resource system does not reach either.
target_link_libraries(Willpower.Application
    PUBLIC  Willpower.Common
            ext::mpp ext::mpp-mesh ext::mpp-program
    PRIVATE ext::Utils
            vendor::fmod vendor::fmodstudio vendor::fsbank)

target_compile_features(Willpower.Application PRIVATE cxx_std_20)
target_compile_options(Willpower.Application PRIVATE /W4)
bw_target_defaults(Willpower.Application)
bw_output_dirs(Willpower.Application "build/vs2026/bin/x64/$<CONFIG>" "build/vs2026/lib/x64/$<CONFIG>")
bw_enable_sdl_checks(Willpower.Application)
bw_add_version_resource(Willpower.Application)
set_target_properties(Willpower.Application PROPERTIES FOLDER Willpower)

# tungsten-oxide's loader test: parses a Resources.yaml through the real
# resource system. Pointed at BooleanWorld's manifest once it is converted.
add_executable(willpower_application_resource_yaml_tests tests/resource_yaml_tests.cpp)
target_compile_features(willpower_application_resource_yaml_tests PRIVATE cxx_std_20)
target_link_libraries(willpower_application_resource_yaml_tests PRIVATE Willpower.Application)
bw_output_dirs(willpower_application_resource_yaml_tests "build/vs2026/bin/x64/$<CONFIG>")
set_target_properties(willpower_application_resource_yaml_tests PROPERTIES FOLDER Willpower)
bw_deploy_runtime_dlls(willpower_application_resource_yaml_tests)
# Willpower.Application.dll imports fmod/fmodstudio, whose import libraries are
# stubs - no DLL for CMake to track, so copy vendor's bin directory as well.
bw_deploy_vendor_dlls(willpower_application_resource_yaml_tests)
add_test(NAME willpower_application_resource_yaml
    COMMAND willpower_application_resource_yaml_tests
        "${BW_ROOT}/Applications/BooleanWorld/app/resources/Resources.yaml")
