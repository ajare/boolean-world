# Imported targets for everything this repo does NOT build:
#
#   - the two git submodules (ext/Utils, ext/MassivePolyPusher), which keep
#     their own Visual Studio solutions and must not be modified;
#   - the prebuilt third-party libraries under vendor/.
#
# Both follow the same convention: Release artefacts are unsuffixed, Debug
# artefacts usually carry a trailing "d". Anything that does not follow it is
# spelled out explicitly below.

set(BW_EXT     "${BW_ROOT}/ext")
set(BW_VENDOR  "${BW_ROOT}/vendor")
set(BW_VENDOR_LIB "${BW_VENDOR}/lib/vs2026/x64")
set(BW_VENDOR_BIN "${BW_VENDOR}/bin/vs2026/x64")

# The Profiling configuration has no counterpart in any prebuilt dependency,
# so every imported target resolves it to Release.
set(CMAKE_MAP_IMPORTED_CONFIG_PROFILING Release "")

# bw_import_shared(<target> <lib-release> <lib-debug> [INCLUDE dirs...])
#
# A prebuilt import library. Also records the matching DLL (same stem, under
# the sibling bin/ directory) so bw_deploy_runtime_dlls can stage it.
function(bw_import_shared name rel_lib dbg_lib)
    cmake_parse_arguments(A "" "" "INCLUDE;DLL_RELEASE;DLL_DEBUG" ${ARGN})
    add_library(${name} SHARED IMPORTED GLOBAL)
    set_target_properties(${name} PROPERTIES
        IMPORTED_IMPLIB_RELEASE   "${rel_lib}"
        IMPORTED_IMPLIB_DEBUG     "${dbg_lib}"
        IMPORTED_IMPLIB           "${rel_lib}"
        MAP_IMPORTED_CONFIG_PROFILING Release)
    if(A_DLL_RELEASE)
        set_target_properties(${name} PROPERTIES
            IMPORTED_LOCATION_RELEASE "${A_DLL_RELEASE}"
            IMPORTED_LOCATION         "${A_DLL_RELEASE}")
    endif()
    if(A_DLL_DEBUG)
        set_target_properties(${name} PROPERTIES IMPORTED_LOCATION_DEBUG "${A_DLL_DEBUG}")
    endif()
    if(A_INCLUDE)
        target_include_directories(${name} INTERFACE ${A_INCLUDE})
    endif()
endfunction()

# bw_import_static(<target> <lib-release> <lib-debug> [INCLUDE dirs...])
function(bw_import_static name rel_lib dbg_lib)
    cmake_parse_arguments(A "" "" "INCLUDE" ${ARGN})
    add_library(${name} STATIC IMPORTED GLOBAL)
    set_target_properties(${name} PROPERTIES
        IMPORTED_LOCATION_RELEASE "${rel_lib}"
        IMPORTED_LOCATION_DEBUG   "${dbg_lib}"
        IMPORTED_LOCATION         "${rel_lib}"
        MAP_IMPORTED_CONFIG_PROFILING Release)
    if(A_INCLUDE)
        target_include_directories(${name} INTERFACE ${A_INCLUDE})
    endif()
endfunction()

# --------------------------------------------------------------------------
# Submodules. Built by their own solutions - see cmake/Submodules.cmake.
# --------------------------------------------------------------------------

# bw_import_submodule(<target> <stem> <lib-dir> <bin-dir> [INCLUDE dirs...])
function(bw_import_submodule name stem libdir bindir)
    cmake_parse_arguments(A "" "" "INCLUDE" ${ARGN})
    bw_import_shared(${name}
        "${libdir}/Release/${stem}.lib"
        "${libdir}/Debug/${stem}d.lib"
        DLL_RELEASE "${bindir}/Release/${stem}.dll"
        DLL_DEBUG   "${bindir}/Debug/${stem}d.dll"
        INCLUDE ${A_INCLUDE})
endfunction()

bw_import_submodule(ext::Utils Utils
    "${BW_EXT}/Utils/build/vs2026/lib/x64"
    "${BW_EXT}/Utils/build/vs2026/bin/x64"
    INCLUDE "${BW_EXT}/Utils/include")

set(_mpp "${BW_EXT}/MassivePolyPusher")
bw_import_submodule(ext::mpp MassivePolyPusher
    "${_mpp}/mpp/build/vs2026/lib/x64" "${_mpp}/mpp/build/vs2026/bin/x64"
    INCLUDE "${_mpp}/mpp/include" "${_mpp}/vendor/include")
bw_import_submodule(ext::mpp-mesh MppMesh
    "${_mpp}/mpp-mesh/build/vs2026/lib/x64" "${_mpp}/mpp-mesh/build/vs2026/bin/x64"
    INCLUDE "${_mpp}/mpp-mesh/include")
bw_import_submodule(ext::mpp-helper MppHelper
    "${_mpp}/mpp-helper/build/vs2026/lib/x64" "${_mpp}/mpp-helper/build/vs2026/bin/x64"
    INCLUDE "${_mpp}/mpp-helper/include")
bw_import_submodule(ext::mpp-program MppProgram
    "${_mpp}/mpp-program/build/vs2026/lib/x64" "${_mpp}/mpp-program/build/vs2026/bin/x64"
    INCLUDE "${_mpp}/mpp-program/include")

# --------------------------------------------------------------------------
# vendor/ - prebuilt third-party static libraries.
# --------------------------------------------------------------------------

# Headers are all rooted at vendor/include; fmod additionally needs fmod/core.
add_library(vendor::headers INTERFACE IMPORTED GLOBAL)
target_include_directories(vendor::headers INTERFACE
    "${BW_VENDOR}/include" "${BW_VENDOR}/include/fmod/core")

# bw_vendor_lib(<target> <release-stem> <debug-stem>)
function(bw_vendor_lib name rel dbg)
    bw_import_static(${name}
        "${BW_VENDOR_LIB}/Release/${rel}.lib"
        "${BW_VENDOR_LIB}/Debug/${dbg}.lib")
    target_link_libraries(${name} INTERFACE vendor::headers)
endfunction()

#              target                release stem      debug stem
bw_vendor_lib(vendor::yaml-cpp       yaml-cpp          yaml-cppd)
bw_vendor_lib(vendor::spdlog         spdlog            spdlogd)
bw_vendor_lib(vendor::fmt            fmt               fmtd)
bw_vendor_lib(vendor::sdl2           SDL2              SDL2d)
bw_vendor_lib(vendor::sdl2main       SDL2main          SDL2maind)
bw_vendor_lib(vendor::performanceapi PerformanceAPI_MD PerformanceAPI_MDd)
# No debug variant shipped - the release library is used in every config.
bw_vendor_lib(vendor::clipper2z      Clipper2Z         Clipper2Z)
bw_vendor_lib(vendor::concurrencpp   concurrencpp      concurrencpp)
bw_vendor_lib(vendor::gtest          gtest             gtest)
bw_vendor_lib(vendor::nfd            nfd               nfd)
bw_vendor_lib(vendor::glew           glew32            glew32)
bw_vendor_lib(vendor::glfw3          glfw3             glfw3)
bw_vendor_lib(vendor::sdl3           SDL3              SDL3)
bw_vendor_lib(vendor::freeimage      FreeImage         FreeImage)
bw_vendor_lib(vendor::fmod           fmod_vc           fmod_vc)
bw_vendor_lib(vendor::fmodstudio     fmodstudio_vc     fmodstudio_vc)
bw_vendor_lib(vendor::fsbank         fsbank_vc         fsbank_vc)

# System libraries.
add_library(vendor::opengl INTERFACE IMPORTED GLOBAL)
target_link_libraries(vendor::opengl INTERFACE opengl32)
