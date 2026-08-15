add_library(Willpower.Application SHARED
    src/ApplicationSettings.cpp
    src/AudioSystem.cpp
    src/InputStateManager.cpp
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

target_compile_definitions(Willpower.Application PRIVATE
    WP_APPLICATION_DLL_EXPORT
    WP_USE_ASSERT_TRACE
    $<$<CONFIG:Debug>:WP_USE_MEMLEAK_TRACKING>)

target_link_libraries(Willpower.Application
    PUBLIC  Willpower.Common Willpower.Collide Willpower.Geometry
            ext::mpp ext::mpp-mesh ext::mpp-program
    PRIVATE ext::Utils vendor::freeimage vendor::fmt
            vendor::fmod vendor::fmodstudio vendor::fsbank)

target_compile_features(Willpower.Application PRIVATE cxx_std_20)
target_compile_options(Willpower.Application PRIVATE /W4)
bw_target_defaults(Willpower.Application)
bw_output_dirs(Willpower.Application "build/vs2026/bin/x64/$<CONFIG>" "build/vs2026/lib/x64/$<CONFIG>")
bw_enable_sdl_checks(Willpower.Application)
bw_add_version_resource(Willpower.Application)
set_target_properties(Willpower.Application PROPERTIES FOLDER Willpower)
