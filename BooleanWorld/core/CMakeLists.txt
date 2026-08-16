# The BooleanWorld engine core, linked statically into every app here.

add_library(core STATIC
    src/AnimatedProperty.cpp
    src/Arrangement.cpp
    src/ArrangementWorldData.cpp
    src/ArrangementWorldDataGenerator.cpp
    src/CirclePolygon.cpp
    src/CircleSegmentPolygon.cpp
    src/DefaultWorldDataGenerator.cpp
    src/DynamicWorldDataGenerator.cpp
    src/Easing.cpp
    src/InfluenceEye.cpp
    src/MaterialDefinition.cpp
    src/MeshPrimitive.cpp
    src/Module.cpp
    src/Primitive.cpp
    src/PrimitiveGroup.cpp
    src/PrimitivePropertySet.cpp
    src/RectanglePolygon.cpp
    src/RegularPolygon.cpp
    src/Serializable.cpp
    src/Serializer.cpp
    src/SquareTiling.cpp
    src/SuperformulaPolygon.cpp
    src/Tiling.cpp
    src/TorusPolygon.cpp
    src/TorusSegmentPolygon.cpp
    src/TransformFlow.cpp
    src/Triangulator.cpp
    src/Utils.cpp
    src/Vertex.cpp
    src/VertexDistribution.cpp
    src/VertexTransformer.cpp
    src/VertexTransformerObject.cpp
    src/World.cpp
    src/WorldDataGenerator.cpp
    src/WorldTriggerLine.cpp
    src/YamlSerializer.cpp
    src/tTransform.cpp)

target_include_directories(core PUBLIC include)

target_compile_definitions(core PRIVATE YAML_CPP_STATIC_DEFINE _CONSOLE)

target_link_libraries(core PUBLIC
    bw-common Willpower.Common
    vendor::concurrencpp ext::yaml-cpp)

target_compile_features(core PRIVATE cxx_std_23)
target_compile_options(core PRIVATE /W3)
bw_target_defaults(core)
bw_output_dirs(core "lib/x64/$<CONFIG>")
bw_enable_sdl_checks(core)
set_target_properties(core PROPERTIES FOLDER BooleanWorld)
