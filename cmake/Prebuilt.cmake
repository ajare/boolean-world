# Imported targets for everything this repo does NOT build:
#
#   - MassivePolyPusher (ext/willpower/ext/massive-poly-pusher), which keeps its
#     own CMake build and must not be modified. We consume its build tree: import
#     libraries under lib/<CONFIG>/, DLLs under bin/<CONFIG>/, and its
#     FetchContent'd dependencies (GLEW, SDL3, yaml-cpp) alongside them.
#     `utils` also comes from there now, via mpp's own ext/utils submodule.
#   - the remaining prebuilt third-party libraries under vendor/.
#
# Both follow the same convention: Release artefacts are unsuffixed, Debug
# artefacts carry a trailing "d". Anything that does not is spelled out below.

set(BW_EXT       "${BW_ROOT}/ext")
set(BW_WILLPOWER "${BW_EXT}/willpower")
set(BW_MPP       "${BW_WILLPOWER}/ext/massive-poly-pusher")
set(BW_VENDOR    "${BW_ROOT}/vendor")
set(BW_VENDOR_LIB "${BW_VENDOR}/lib/vs2026/x64")
set(BW_VENDOR_BIN "${BW_VENDOR}/bin/vs2026/x64")

if(WIN32)
set(BW_WILLPOWER_LIB "${BW_WILLPOWER_BUILD_DIR}/lib")
set(BW_WILLPOWER_BIN "${BW_WILLPOWER_BUILD_DIR}/bin")
set(BW_MPP_LIB "${BW_MPP_OUTPUT_DIR}/lib")
set(BW_MPP_BIN "${BW_MPP_OUTPUT_DIR}/bin")
set(BW_MPP_GLEW_INCLUDE_DIR "${BW_MPP_BUILD_DIR}/_deps/glew-2.3.1/include")

# bw_import_shared(<target> <lib-release> <lib-debug> [INCLUDE dirs...])
function(bw_import_shared name rel_lib dbg_lib)
    cmake_parse_arguments(A "" "" "INCLUDE;DLL_RELEASE;DLL_DEBUG" ${ARGN})
    add_library(${name} SHARED IMPORTED GLOBAL)
    set_target_properties(${name} PROPERTIES
        IMPORTED_IMPLIB_RELEASE   "${rel_lib}"
        IMPORTED_IMPLIB_DEBUG     "${dbg_lib}"
        IMPORTED_IMPLIB           "${rel_lib}")
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
        IMPORTED_LOCATION         "${rel_lib}")
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

# MassivePolyPusher.dll itself loads MppData.dll and glew32.dll at runtime.
# CMake cannot see an imported library's own dependencies, so TARGET_RUNTIME_DLLS
# would omit them and every executable would die with 0xC0000135 - state them.
set_property(TARGET ext::mpp APPEND PROPERTY
    INTERFACE_LINK_LIBRARIES ext::mpp-data ext::glew)

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
bw_vendor_lib(vendor::performanceapi PerformanceAPI_MD PerformanceAPI_MDd)
bw_vendor_lib(vendor::gtest          gtest             gtest)
bw_vendor_lib(vendor::nfd            nfd               nfd)

# The vendored FMOD Engine API and the installed FMOD Studio authoring tool
# must be pinned to the same point release: banks build under an equal or
# newer runtime, never an older one.
set(BW_FMOD_VERSION "2.03.14" CACHE STRING "Pinned FMOD Engine/Studio point release" FORCE)
bw_vendor_lib(vendor::fmod           fmod_vc           fmod_vc)
bw_vendor_lib(vendor::fmodstudio     fmodstudio_vc     fmodstudio_vc)
bw_vendor_lib(vendor::fsbank         fsbank_vc         fsbank_vc)

# System libraries.
add_library(vendor::opengl INTERFACE IMPORTED GLOBAL)
target_link_libraries(vendor::opengl INTERFACE opengl32)

# --------------------------------------------------------------------------
# Willpower's standalone build tree.
# --------------------------------------------------------------------------

function(bw_import_willpower target module)
    bw_import_shared(${target}
        "${BW_WILLPOWER_LIB}/Release/${target}/${target}.lib"
        "${BW_WILLPOWER_LIB}/Debug/${target}/${target}d.lib"
        DLL_RELEASE "${BW_WILLPOWER_BIN}/Release/${target}/${target}.dll"
        DLL_DEBUG "${BW_WILLPOWER_BIN}/Debug/${target}/${target}d.dll"
        INCLUDE "${BW_WILLPOWER}/${module}/include")
endfunction()

bw_import_willpower(Willpower.Common willpower.common)
bw_import_willpower(Willpower.Geometry willpower.geometry)
bw_import_willpower(Willpower.Wayfinder willpower.wayfinder)
bw_import_willpower(Willpower.Collide willpower.collide)
bw_import_willpower(Willpower.Application willpower.application)
bw_import_willpower(WillPower.Viz willpower.viz)

target_link_libraries(Willpower.Common INTERFACE vendor::headers)
target_link_libraries(Willpower.Geometry INTERFACE
    Willpower.Common vendor::headers)
target_link_libraries(Willpower.Wayfinder INTERFACE
    Willpower.Common Willpower.Geometry)
target_link_libraries(Willpower.Collide INTERFACE
    Willpower.Common Willpower.Geometry)
target_link_libraries(Willpower.Application INTERFACE
    Willpower.Common ext::mpp ext::mpp-mesh ext::mpp-program)
target_link_libraries(WillPower.Viz INTERFACE
    Willpower.Common Willpower.Collide
    ext::mpp ext::mpp-mesh ext::mpp-helper ext::mpp-program)

else()
# Linux consumes the artifacts produced by the standalone Willpower build and
# by MassivePolyPusher's existing source-tree output layout.
set(BW_MPP_BIN "${BW_MPP_OUTPUT_DIR}/bin/${CMAKE_BUILD_TYPE}")
set(BW_MPP_LIB "${BW_MPP_OUTPUT_DIR}/lib/${CMAKE_BUILD_TYPE}")
set(BW_WILLPOWER_BIN "${BW_WILLPOWER_BUILD_DIR}/bin/${CMAKE_BUILD_TYPE}")
set(BW_MPP_GLEW_INCLUDE_DIR "${BW_MPP_BUILD_DIR}/_deps/glew-2.3.1/include")

function(bw_linux_shared name location)
    add_library(${name} SHARED IMPORTED GLOBAL)
    set_target_properties(${name} PROPERTIES IMPORTED_LOCATION "${location}")
endfunction()
function(bw_linux_mpp name stem include_dir)
    bw_linux_shared(${name} "${BW_MPP_BIN}/lib${stem}.so")
    target_include_directories(${name} INTERFACE ${include_dir})
endfunction()

bw_linux_mpp(ext::Utils Utils "${BW_MPP}/ext/utils/include")
bw_linux_mpp(ext::mpp MassivePolyPusher "${BW_MPP}/mpp/include;${BW_MPP}/vendor/include;${BW_MPP_GLEW_INCLUDE_DIR}")
bw_linux_mpp(ext::mpp-mesh MppMesh "${BW_MPP}/mpp-mesh/include")
bw_linux_mpp(ext::mpp-helper MppHelper "${BW_MPP}/mpp-helper/include")
bw_linux_mpp(ext::mpp-program MppProgram "${BW_MPP}/mpp-program/include")
bw_linux_mpp(ext::mpp-data MppData "${BW_MPP}/mpp-data/include")
bw_linux_mpp(ext::sdl3 SDL3 "${BW_MPP}/ext/sdl/include")
bw_linux_mpp(ext::glew GLEW "${BW_MPP_GLEW_INCLUDE_DIR}")
set_property(TARGET ext::glew APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS GLEW_NO_GLU)
set_property(TARGET ext::mpp APPEND PROPERTY INTERFACE_LINK_LIBRARIES ext::mpp-data ext::glew)

add_library(ext::mpp-app-support STATIC IMPORTED GLOBAL)
set_target_properties(ext::mpp-app-support PROPERTIES
    IMPORTED_LOCATION "${BW_MPP_LIB}/libMppAppSupport.a"
    INTERFACE_INCLUDE_DIRECTORIES "${BW_MPP}/mpp-app-support/include")
add_library(ext::yaml-cpp STATIC IMPORTED GLOBAL)
set_target_properties(ext::yaml-cpp PROPERTIES
    IMPORTED_LOCATION "${BW_MPP_LIB}/libyaml-cpp.a"
    INTERFACE_INCLUDE_DIRECTORIES "${BW_MPP}/ext/utils/vendor/yaml-cpp/include")

add_library(vendor::headers INTERFACE IMPORTED GLOBAL)
target_include_directories(vendor::headers INTERFACE "${BW_VENDOR}/include")
add_library(vendor::spdlog INTERFACE IMPORTED GLOBAL)
target_link_libraries(vendor::spdlog INTERFACE vendor::headers)
target_compile_definitions(vendor::spdlog INTERFACE SPDLOG_HEADER_ONLY)
# Native file dialogs and Superluminal are currently Windows-only consumers.
add_library(vendor::nfd INTERFACE IMPORTED GLOBAL)
add_library(vendor::performanceapi INTERFACE IMPORTED GLOBAL)
target_link_libraries(vendor::nfd INTERFACE vendor::headers)
target_link_libraries(vendor::performanceapi INTERFACE vendor::headers)

find_package(OpenGL REQUIRED)
add_library(vendor::opengl INTERFACE IMPORTED GLOBAL)
target_link_libraries(vendor::opengl INTERFACE OpenGL::GL ${CMAKE_DL_LIBS})

function(bw_linux_willpower target module)
    bw_linux_shared(${target} "${BW_WILLPOWER_BIN}/${target}/lib${target}.so")
    target_include_directories(${target} INTERFACE "${BW_WILLPOWER}/${module}/include")
endfunction()
bw_linux_willpower(Willpower.Common willpower.common)
bw_linux_willpower(Willpower.Geometry willpower.geometry)
bw_linux_willpower(Willpower.Wayfinder willpower.wayfinder)
bw_linux_willpower(Willpower.Collide willpower.collide)
bw_linux_willpower(Willpower.Application willpower.application)
bw_linux_willpower(WillPower.Viz willpower.viz)
target_link_libraries(Willpower.Common INTERFACE vendor::headers)
target_link_libraries(Willpower.Geometry INTERFACE Willpower.Common vendor::headers)
target_link_libraries(Willpower.Wayfinder INTERFACE Willpower.Common Willpower.Geometry)
target_link_libraries(Willpower.Collide INTERFACE Willpower.Common Willpower.Geometry)
target_link_libraries(Willpower.Application INTERFACE Willpower.Common ext::mpp ext::mpp-mesh ext::mpp-program)
target_link_libraries(WillPower.Viz INTERFACE Willpower.Common Willpower.Collide ext::mpp ext::mpp-mesh ext::mpp-helper ext::mpp-program)
endif()
