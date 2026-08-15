# Replaces Willpower/build/vs2026/Willpower.sln.
#
# Listed in dependency order for readability; CMake resolves the actual order
# from target_link_libraries.

add_subdirectory(willpower.common)
add_subdirectory(willpower.geometry)
add_subdirectory(willpower.collide)
add_subdirectory(willpower.application)
add_subdirectory(willpower.firepower)
add_subdirectory(willpower.viz)
