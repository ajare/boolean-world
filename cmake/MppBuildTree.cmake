# MppBuildTree.cmake — locate MassivePolyPusher's build tree.
#
# Adapted from tungsten-oxide's cpp/cmake/MppBuildTree.cmake.
#
# MassivePolyPusher is not part of this CMake project: it keeps its own build,
# and we consume that build tree directly — import libraries under
# lib/<CONFIG>/, runtime DLLs under bin/<CONFIG>/, and its FetchContent'd
# dependencies (GLEW, SDL3, assimp, yaml-cpp) under _deps/.
#
# Where that tree lands is the mpp builder's choice: `-B build` and
# `-B build/cmake` are both in use and produce identical internal layouts, so
# probe for it rather than hardcoding one spelling. Guessing wrong otherwise
# surfaces much later as "Cannot open include file: 'GL/glew.h'" from deep
# inside an mpp header, naming neither mpp nor the path actually searched.
include_guard(GLOBAL)

# Sets, in the caller's scope:
#   BW_MPP_BUILD_DIR         the resolved build tree root
#   BW_MPP_GLEW_INCLUDE_DIR  the fetched GLEW's include directory inside it
function(bw_resolve_mpp_build_tree mpp_source_dir)
    set(_default "${mpp_source_dir}/build")
    set(_build_dir "")
    foreach(_candidate "${_default}" "${mpp_source_dir}/build/cmake")
        # Test for CMakeCache.txt, not just the directory: `build/` exists as
        # the parent of `build/cmake` in the nested layout, so a plain EXISTS
        # check would match it and resolve to an empty tree.
        if(EXISTS "${_candidate}/CMakeCache.txt")
            set(_build_dir "${_candidate}")
            break()
        endif()
    endforeach()

    if(_build_dir STREQUAL "")
        set(_build_dir "${_default}")
        message(FATAL_ERROR
            "MassivePolyPusher has no build tree under '${mpp_source_dir}/build' or "
            "'${mpp_source_dir}/build/cmake'. Build it first:\n"
            "  cmake -S ext/massive-poly-pusher -B ext/massive-poly-pusher/build -G \"Visual Studio 18 2026\" -A x64\n"
            "  cmake --build ext/massive-poly-pusher/build --config Release --parallel\n"
            "RebuildAll.bat does this for you.")
    endif()

    # GLEW arrives through FetchContent, so its directory carries the fetched
    # version number. Glob rather than pinning one, so bumping mpp's GLEW does
    # not silently drop the include directory.
    file(GLOB _glew_includes "${_build_dir}/_deps/glew-*/include")
    if(_glew_includes)
        list(GET _glew_includes 0 _glew_include)
    else()
        set(_glew_include "${_build_dir}/_deps/glew-2.3.1/include")
    endif()

    set(BW_MPP_BUILD_DIR "${_build_dir}" PARENT_SCOPE)
    set(BW_MPP_GLEW_INCLUDE_DIR "${_glew_include}" PARENT_SCOPE)
endfunction()
