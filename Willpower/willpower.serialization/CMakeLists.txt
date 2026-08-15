add_library(Willpower.Serialization SHARED
    src/SerializationUtils.cpp
    src/SerializerChunk.cpp)

target_include_directories(Willpower.Serialization PUBLIC include)

target_compile_definitions(Willpower.Serialization PRIVATE
    WIN32 _WINDOWS _USRDLL
    WP_SERIALIZATION_DLL_EXPORT
    WP_USE_ASSERT_TRACE
    $<$<CONFIG:Debug>:WP_USE_MEMLEAK_TRACKING>)

target_link_libraries(Willpower.Serialization PUBLIC Willpower.Common ext::Utils vendor::headers)

target_compile_features(Willpower.Serialization PRIVATE cxx_std_20)
target_compile_options(Willpower.Serialization PRIVATE /W4)
bw_target_defaults(Willpower.Serialization)
bw_output_dirs(Willpower.Serialization "build/vs2026/bin/x64/$<CONFIG>" "build/vs2026/lib/x64/$<CONFIG>")
bw_add_version_resource(Willpower.Serialization)
set_target_properties(Willpower.Serialization PROPERTIES FOLDER Willpower)
