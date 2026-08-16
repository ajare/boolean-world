# Replaces src/AppLib/build/vs2026/AppLib.sln.

add_library(AppLib SHARED
    src/AnimationDatabase.cpp
    src/Entity.cpp
    src/EntityFacade.cpp
    src/EntityHandler.cpp
    src/EntityManager.cpp
    src/ImageSetTiledDefinitionFactory.cpp
    src/Map.cpp
    src/MapDefaultDefinitionFactory.cpp
    src/MapResourceDefinitionFactory.cpp
    src/MapTiledDefinitionFactory.cpp
    src/ModelInstance.cpp
    src/PlayObjectCreators.cpp
    src/ProtoEntity.cpp
    src/ProtoEntityDefaultDefinitionFactory.cpp
    src/ProtoEntityResourceDefinitionFactory.cpp
    src/ScreenFxManager.cpp
    src/State.cpp
    src/StateController.cpp
    src/StateLoad.cpp
    src/StateMapLoad.cpp
    src/StateMapTransition.cpp
    src/StateMapUnload.cpp
    src/StatePlay.cpp
    src/StateUnload.cpp
    src/ThreadableLoadState.cpp
    src/VisualSpriteDataProvider.cpp
    src/VisualSpriteEntityFacade.cpp
    src/VisualTriMeshEntityFacade.cpp)

# Consumers include "applib/Foo.h"; AppLib's own sources include some of those
# headers by bare name ("ModelInstance.h"), so it needs include/applib too.
target_include_directories(AppLib
    PUBLIC  include
    PRIVATE include/applib)

target_compile_definitions(AppLib PRIVATE
    APPLIB_DLL_EXPORT
    _SILENCE_CXX17_ALLOCATOR_VOID_DEPRECATION_WARNING
    _SILENCE_CXX23_DENORM_DEPRECATION_WARNING)

target_link_libraries(AppLib PUBLIC
    Willpower.Application Willpower.Common Willpower.Collide
    Willpower.Geometry WillPower.Viz
    ext::mpp ext::mpp-mesh ext::mpp-helper ext::Utils
    vendor::headers)

target_compile_features(AppLib PRIVATE cxx_std_23)
target_compile_options(AppLib PRIVATE /W3)
bw_target_defaults(AppLib)
bw_output_dirs(AppLib "build/vs2026/bin/x64/$<CONFIG>" "build/vs2026/lib/x64/$<CONFIG>")
bw_enable_sdl_checks(AppLib)
set_target_properties(AppLib PROPERTIES FOLDER AppLib)

if(BUILD_TESTING)
    add_executable(applib_proto_entity_schema_tests
        tests/proto_entity_schema_tests.cpp)
    target_link_libraries(applib_proto_entity_schema_tests PRIVATE AppLib)
    target_compile_features(applib_proto_entity_schema_tests PRIVATE cxx_std_23)
    bw_target_defaults(applib_proto_entity_schema_tests)
    bw_output_dirs(applib_proto_entity_schema_tests "build/vs2026/bin/x64/$<CONFIG>")
    bw_deploy_runtime_dlls(applib_proto_entity_schema_tests)
    bw_deploy_vendor_dlls(applib_proto_entity_schema_tests)
    add_test(NAME applib_proto_entity_schema COMMAND applib_proto_entity_schema_tests)
endif()
