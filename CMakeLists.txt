cmake_minimum_required(VERSION 3.25)

project(BooleanWorld LANGUAGES CXX)

# --------------------------------------------------------------------------
# This build replaces four Visual Studio solutions: Willpower.sln, AppLib.sln,
# BooleanWorld.sln and Launcher.sln. Willpower and MassivePolyPusher keep their
# own builds and are consumed as prebuilt imported libraries. Only projects
# under src/ are included in the generated BooleanWorld solution. See
# cmake/Submodules.cmake and cmake/Prebuilt.cmake.
# --------------------------------------------------------------------------

set(BW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
list(APPEND CMAKE_MODULE_PATH "${BW_ROOT}/cmake")

if(NOT MSVC)
    message(FATAL_ERROR "BooleanWorld is a Windows/MSVC build.")
endif()

# x64 only: vendor/lib ships x64 libraries exclusively.
if(CMAKE_SIZEOF_VOID_P AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Only x64 is supported (vendor/lib is x64-only). Configure with -A x64.")
endif()

# Debug and Release, as the original solutions had.
if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Every .vcxproj set CharacterSet=Unicode. This is load-bearing, not cosmetic:
# ApplicationDLL::load builds a wstring and calls LoadLibrary, which only
# resolves to LoadLibraryW when UNICODE is defined.
add_compile_definitions(UNICODE _UNICODE)

# All generated binaries live below this CMake build tree. Each target uses an
# isolated output directory configured by bw_output_dirs().

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(CTest)   # must be at top level for ctest to find tests in subdirectories

include(Submodules)
bw_ensure_willpower()

include(Prebuilt)
include(Helpers)

add_subdirectory(src/AppLib)
add_subdirectory(src/BooleanWorld)
add_subdirectory(src/Launcher)

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT Launcher)
