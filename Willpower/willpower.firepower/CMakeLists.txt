add_library(Willpower.Firepower SHARED
    src/Bomb.cpp
    src/Mesh.cpp
    src/MeshCollisionManager.cpp)

target_include_directories(Willpower.Firepower PUBLIC include)

target_compile_definitions(Willpower.Firepower PRIVATE
    WIN32 _WINDOWS
    WP_FIREPOWER_DLL_EXPORT
    WP_USE_ASSERT_TRACE
    $<$<CONFIG:Debug>:WP_USE_MEMLEAK_TRACKING>
    $<$<NOT:$<CONFIG:Debug>>:WP_PROFILE_BUILD>)

target_link_libraries(Willpower.Firepower
    PUBLIC  Willpower.Common Willpower.Application Willpower.Geometry
            ext::mpp ext::mpp-mesh ext::mpp-program
    PRIVATE ext::Utils vendor::headers)

target_compile_features(Willpower.Firepower PRIVATE cxx_std_20)
target_compile_options(Willpower.Firepower PRIVATE /W4)
bw_target_defaults(Willpower.Firepower)
bw_output_dirs(Willpower.Firepower "build/vs2026/bin/x64/$<CONFIG>" "build/vs2026/lib/x64/$<CONFIG>")
bw_add_version_resource(Willpower.Firepower)
set_target_properties(Willpower.Firepower PROPERTIES FOLDER Willpower)
