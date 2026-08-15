# Imported targets for everything this repo does NOT build:
#
#   - MassivePolyPusher (ext/massive-poly-pusher), which keeps its own CMake
#     build and must not be modified. We consume its build tree: import
#     libraries under lib/<CONFIG>/, DLLs under bin/<CONFIG>/, and its
#     FetchContent'd dependencies (GLEW, SDL3, yaml-cpp) alongside them.
#     `utils` also comes from there now, via mpp's own ext/utils submodule.
#   - the remaining prebuilt third-party libraries under vendor/.
#
# Both follow the same convention: Release artefacts are unsuffixed, Debug
# artefacts carry a trailing "d". Anything that does not is spelled out below.

set(BW_EXT     "${BW_ROOT}/ext")
set(BW_MPP     "${BW_EXT}/massive-poly-pusher")
set(BW_VENDOR  "${BW_ROOT}/vendor")
set(BW_VENDOR_LIB "${BW_VENDOR}/lib/vs2026/x64")
set(BW_VENDOR_BIN "${BW_VENDOR}/bin/vs2026/x64")

include(MppBuildTree)
bw_resolve_mpp_build_tree("${BW_MPP}")

set(BW_MPP_LIB "${BW_MPP_BUILD_DIR}/lib")
set(BW_MPP_BIN "${BW_MPP_BUILD_DIR}/bin")

# The Profiling configuration has no counterpart in any prebuilt dependency,
# so every imported target resolves it to Release.
set(CMAKE_MAP_IMPORTED_CONFIG_PROFILING Release "")

# bw_import_shared(<target> <lib-release> <lib-debug> [INCLUDE dirs...])
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
# MassivePolyPusher's build tree.
# --------------------------------------------------------------------------

# bw_import_mpp(<target> <stem> [INCLUDE dirs...])
function(bw_import_mpp name stem)
    cmake_parse_arguments(A "" "" "INCLUDE" ${ARGN})
    bw_import_shared(${name}
        "${BW_MPP_LIB}/Release/${stem}.lib"
        "${BW_MPP_LIB}/Debug/${stem}d.lib"
        DLL_RELEASE "${BW_MPP_BIN}/Release/${stem}.dll"
        DLL_DEBUG   "${BW_MPP_BIN}/Debug/${stem}d.dll"
        INCLUDE ${A_INCLUDE})
endfunction()

bw_import_mpp(ext::Utils Utils        INCLUDE "${BW_MPP}/ext/utils/include")
bw_import_mpp(ext::mpp MassivePolyPusher
    INCLUDE "${BW_MPP}/mpp/include" "${BW_MPP}/vendor/include" "${BW_MPP_GLEW_INCLUDE_DIR}")
bw_import_mpp(ext::mpp-mesh    MppMesh    INCLUDE "${BW_MPP}/mpp-mesh/include")
bw_import_mpp(ext::mpp-helper  MppHelper  INCLUDE "${BW_MPP}/mpp-helper/include")
bw_import_mpp(ext::mpp-program MppProgram INCLUDE "${BW_MPP}/mpp-program/include")
bw_import_mpp(ext::mpp-data    MppData    INCLUDE "${BW_MPP}/mpp-data/include")

# MppAppSupport is a static library in mpp's build.
bw_import_static(ext::mpp-app-support
    "${BW_MPP_LIB}/Release/MppAppSupport.lib"
    "${BW_MPP_LIB}/Debug/MppAppSupportd.lib"
    INCLUDE "${BW_MPP}/mpp-app-support/include")

# SDL3, GLEW and yaml-cpp now come from mpp's build rather than vendor/.
bw_import_shared(ext::sdl3
    "${BW_MPP_LIB}/Release/SDL3.lib" "${BW_MPP_LIB}/Debug/SDL3d.lib"
    DLL_RELEASE "${BW_MPP_BIN}/Release/SDL3.dll"
    DLL_DEBUG   "${BW_MPP_BIN}/Debug/SDL3d.dll"
    INCLUDE "${BW_MPP}/ext/sdl/include")
bw_import_shared(ext::glew
    "${BW_MPP_LIB}/Release/glew32.lib" "${BW_MPP_LIB}/Debug/glew32d.lib"
    DLL_RELEASE "${BW_MPP_BIN}/Release/glew32.dll"
    DLL_DEBUG   "${BW_MPP_BIN}/Debug/glew32d.dll"
    INCLUDE "${BW_MPP_GLEW_INCLUDE_DIR}")
bw_import_static(ext::yaml-cpp
    "${BW_MPP_LIB}/Release/yaml-cpp.lib" "${BW_MPP_LIB}/Debug/yaml-cppd.lib"
    INCLUDE "${BW_MPP}/ext/utils/vendor/yaml-cpp/include")

# --------------------------------------------------------------------------
# vendor/ - the third-party libraries mpp does not supply.
# --------------------------------------------------------------------------

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
# GLFW is NOT supplied by mpp (its ext/ is assimp, glew, imgui, sdl, utils),
# so the Launcher's GLFW backend keeps using vendor's copy.
bw_vendor_lib(vendor::glfw3          glfw3             glfw3)
bw_vendor_lib(vendor::freeimage      FreeImage         FreeImage)
bw_vendor_lib(vendor::fmod           fmod_vc           fmod_vc)
bw_vendor_lib(vendor::fmodstudio     fmodstudio_vc     fmodstudio_vc)
bw_vendor_lib(vendor::fsbank         fsbank_vc         fsbank_vc)

# System libraries.
add_library(vendor::opengl INTERFACE IMPORTED GLOBAL)
target_link_libraries(vendor::opengl INTERFACE opengl32)
