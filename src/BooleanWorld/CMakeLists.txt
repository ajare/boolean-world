# Headers shared by every BooleanWorld target (common/GameDefines.h,
# common/MaterialRegistry.h). Header-only, so an INTERFACE target.
add_library(bw-common INTERFACE)
target_include_directories(bw-common INTERFACE common/include)

add_subdirectory(core)
add_subdirectory(core-dll)
add_subdirectory(app)
add_subdirectory(editor)
add_subdirectory(profiler)

if(BUILD_TESTING)
    add_executable(boolean_world_material_registry_tests
        common/tests/material_registry_tests.cpp
        common/tests/material_registry_other_translation_unit.cpp)
    target_link_libraries(boolean_world_material_registry_tests PRIVATE core)
    target_compile_features(boolean_world_material_registry_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_material_registry_tests)
    bw_output_dirs(boolean_world_material_registry_tests)
    bw_deploy_runtime_dlls(boolean_world_material_registry_tests)
    add_test(NAME boolean_world_material_registry
             COMMAND boolean_world_material_registry_tests)
endif()
