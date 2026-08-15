add_library(Willpower.Common SHARED
    src/AccelerationGrid.cpp
    src/BezierSpline.cpp
    src/BoundingBox.cpp
    src/BoundingCircle.cpp
    src/BoundingConvexPolygon.cpp
    src/CachedStaticAccelerationGrid.cpp
    src/ExtentsCalculator.cpp
    src/LineHit.cpp
    src/Logger.cpp
    src/MathsUtils.cpp
    src/Renderable.cpp
    src/SplinePath.cpp
    src/StackWalker.cpp
    src/Timer.cpp
    src/Triangulation.cpp
    src/Vector2.cpp
    src/WillpowerWalker.cpp)

target_include_directories(Willpower.Common PUBLIC include)

target_compile_definitions(Willpower.Common PRIVATE
    WIN32 _WINDOWS _USRDLL
    WP_COMMON_DLL_EXPORT
    WP_USE_ASSERT_TRACE
    TINYXML2_EXPORT
    $<$<CONFIG:Debug>:WP_USE_MEMLEAK_TRACKING>)

target_link_libraries(Willpower.Common PUBLIC ext::mpp-mesh ext::Utils vendor::headers)

target_compile_features(Willpower.Common PRIVATE cxx_std_20)
target_compile_options(Willpower.Common PRIVATE /W4)
bw_target_defaults(Willpower.Common)
bw_output_dirs(Willpower.Common "build/vs2026/bin/x64/$<CONFIG>" "build/vs2026/lib/x64/$<CONFIG>")
bw_add_version_resource(Willpower.Common)
set_target_properties(Willpower.Common PROPERTIES FOLDER Willpower)
