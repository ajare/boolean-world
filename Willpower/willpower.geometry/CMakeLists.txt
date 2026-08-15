add_library(Willpower.Geometry SHARED
    src/BezierOffsetter.cpp
    src/ConvexOffsetter.cpp
    src/DirectedEdgeLoop.cpp
    src/Edge.cpp
    src/EdgeFilter.cpp
    src/Filter.cpp
    src/Mesh.cpp
    src/MeshHelpers.cpp
    src/MeshOperations.cpp
    src/MeshQuery.cpp
    src/MeshUtils.cpp
    src/Offsetter.cpp
    src/Polygon.cpp
    src/SerializerMeshChunk.cpp
    src/TypeConverters.cpp
    src/Vertex.cpp
    src/clipper.cpp)

target_include_directories(Willpower.Geometry PUBLIC include)

target_compile_definitions(Willpower.Geometry PRIVATE
    WIN32 _WINDOWS _USRDLL
    WP_GEOMETRY_DLL_EXPORT
    WP_USE_ASSERT_TRACE
    $<$<CONFIG:Debug>:WP_USE_MEMLEAK_TRACKING>)

# Serialization is PUBLIC: geometry/SerializerMeshChunk.h exposes it, and it is
# the only path by which anything in this repo reaches willpower.serialization.
target_link_libraries(Willpower.Geometry
    PUBLIC  Willpower.Common Willpower.Serialization
    PRIVATE ext::Utils vendor::fmt)

target_compile_features(Willpower.Geometry PRIVATE cxx_std_20)
target_compile_options(Willpower.Geometry PRIVATE /W4)
bw_target_defaults(Willpower.Geometry)
bw_output_dirs(Willpower.Geometry "build/vs2026/bin/x64/$<CONFIG>" "build/vs2026/lib/x64/$<CONFIG>")
bw_add_version_resource(Willpower.Geometry)
set_target_properties(Willpower.Geometry PROPERTIES FOLDER Willpower)
