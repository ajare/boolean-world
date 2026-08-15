# Adopted wholesale from tungsten-oxide (cpp/willpower/willpower.geometry).
#
# SerializerMeshChunk is gone along with willpower.serialization: it was that
# module's only route into this codebase and nothing outside geometry used it.

add_library(Willpower.Geometry SHARED
    src/BezierOffsetter.cpp
    src/ConvexOffsetter.cpp
    src/CsgUtils.cpp
    src/DirectedEdgeLoop.cpp
    src/Edge.cpp
    src/EdgeFilter.cpp
    src/Filter.cpp
    src/Mesh.cpp
    src/MeshHelpers.cpp
    src/MeshOperations.cpp
    src/MeshQuery.cpp
    src/MeshUtils.cpp
    src/MeshValidator.cpp
    src/Offsetter.cpp
    src/Polygon.cpp
    src/PolygonFilter.cpp
    src/TypeConverters.cpp
    src/Vertex.cpp
    src/VertexFilter.cpp
    src/clipper.cpp)

target_include_directories(Willpower.Geometry PUBLIC include)

target_compile_definitions(Willpower.Geometry PRIVATE
    WIN32 _WINDOWS _USRDLL
    WP_GEOMETRY_DLL_EXPORT
    WP_USE_ASSERT_TRACE
    $<$<CONFIG:Debug>:WP_USE_MEMLEAK_TRACKING>)

# vendor::headers supplies mapbox (earcut).
target_link_libraries(Willpower.Geometry PUBLIC Willpower.Common vendor::headers)

target_compile_features(Willpower.Geometry PRIVATE cxx_std_20)
target_compile_options(Willpower.Geometry PRIVATE /W4)
bw_target_defaults(Willpower.Geometry)
bw_output_dirs(Willpower.Geometry "build/vs2026/bin/x64/$<CONFIG>" "build/vs2026/lib/x64/$<CONFIG>")
bw_add_version_resource(Willpower.Geometry)
set_target_properties(Willpower.Geometry PROPERTIES FOLDER Willpower)
