# Replaces BooleanWorld/build/vs2026/BooleanWorld.sln.

# Headers shared by every BooleanWorld target (common/GameDefines.h,
# common/MaterialRegistry.h). Header-only, so an INTERFACE target.
add_library(bw-common INTERFACE)
target_include_directories(bw-common INTERFACE common/include)

add_subdirectory(core)
add_subdirectory(core-dll)
add_subdirectory(app)
add_subdirectory(editor)
add_subdirectory(profiler)
