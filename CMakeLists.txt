# 3.26 for generator expressions in add_custom_command(COMMENT ...),
# used to keep the AddressSanitizer-staging step silent outside MemCheck.
cmake_minimum_required(VERSION 3.26)

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

# Debug and Release, as the original solutions had, plus MemCheck: a Debug
# build instrumented with AddressSanitizer for finding memory errors and
# leaks. Mirrors the ASan setup already used by
# launcher_zip_resource_location_tests (see src/Launcher/CMakeLists.txt).
if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release;MemCheck" CACHE STRING "" FORCE)
endif()

# Willpower/MassivePolyPusher/vendor are prebuilt without ASan, so their
# objects carry the MSVC STL's container-overflow annotation markers at 0
# while our instrumented objects carry them at 1, and the linker rejects the
# mismatch (LNK2038 for annotate_vector/annotate_string/annotate_optional).
# Disable the annotations on our side so they match the libraries we can't
# rebuild.
set(CMAKE_CXX_FLAGS_MEMCHECK
    "/Zi /Ob0 /Od /fsanitize=address /D_DISABLE_VECTOR_ANNOTATION /D_DISABLE_STRING_ANNOTATION /D_DISABLE_OPTIONAL_ANNOTATION"
    CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_MEMCHECK   "${CMAKE_CXX_FLAGS_MEMCHECK}" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_MEMCHECK
    "/DEBUG /INCREMENTAL:NO /INFERASANLIBS" CACHE STRING "" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_MEMCHECK
    "${CMAKE_EXE_LINKER_FLAGS_MEMCHECK}" CACHE STRING "" FORCE)

# There is no separate MemCheck build of Willpower/MassivePolyPusher - link
# MemCheck against the same prebuilt Debug import libraries and DLLs Debug
# uses (see cmake/Submodules.cmake and cmake/Prebuilt.cmake).
set(CMAKE_MAP_IMPORTED_CONFIG_MEMCHECK "Debug" CACHE STRING "" FORCE)

# MSVC's AddressSanitizer does not implement LeakSanitizer at all -
# detect_leaks=1 is rejected outright ("not supported on this platform"),
# not just a no-op, so it is deliberately left unset here. Run under
# Application Verifier or the CRT debug heap for leak detection instead.
#
# Launcher is a WIN32-subsystem executable with no console, so ASan's report
# (normally printed to stderr before it __debugbreak()s) has nowhere to go -
# point it at a log file instead so a caught error is actually visible.
set(BW_MEMCHECK_ASAN_LOG_DIR "${CMAKE_BINARY_DIR}/asan-logs")
file(MAKE_DIRECTORY "${BW_MEMCHECK_ASAN_LOG_DIR}")
# log_path's value must be quoted: ASAN_OPTIONS is colon-separated, and the
# drive letter in an absolute Windows path (D:/...) is itself a colon, which
# splits the option in two without quoting.
set(BW_MEMCHECK_ENVIRONMENT
    "ASAN_OPTIONS=log_path='${BW_MEMCHECK_ASAN_LOG_DIR}/asan'")

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
