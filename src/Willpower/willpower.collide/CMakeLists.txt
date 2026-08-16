add_library(Willpower.Collide SHARED
    src/CollideAABB.cpp
    src/Collider.cpp
    src/ColliderCircle.cpp
    src/Simulation.cpp
    src/StaticLine.cpp)

target_include_directories(Willpower.Collide PUBLIC include)

target_compile_definitions(Willpower.Collide PRIVATE
    WIN32 _WINDOWS _USRDLL
    WP_COLLIDE_DLL_EXPORT
    WP_USE_ASSERT_TRACE
    $<$<CONFIG:Debug>:WP_USE_MEMLEAK_TRACKING>)

target_link_libraries(Willpower.Collide
    PUBLIC  Willpower.Common Willpower.Geometry
    PRIVATE ext::Utils vendor::headers)

target_compile_features(Willpower.Collide PRIVATE cxx_std_20)
target_compile_options(Willpower.Collide PRIVATE /W4)
bw_target_defaults(Willpower.Collide)
bw_output_dirs(Willpower.Collide "build/vs2026/bin/x64/$<CONFIG>" "build/vs2026/lib/x64/$<CONFIG>")
set_target_properties(Willpower.Collide PROPERTIES FOLDER Willpower)
