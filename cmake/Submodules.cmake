# MassivePolyPusher keeps its own CMake build and is never modified by this
# project. Its import libraries only have to exist before anything here links,
# so configure and build it on demand if they are missing.
#
# Set BW_BUILD_MPP=OFF to manage it yourself.

option(BW_BUILD_MPP "Configure and build ext/massive-poly-pusher if it has not been built" ON)

set(BW_MPP_SOURCE_DIR "${BW_ROOT}/ext/massive-poly-pusher")

# One representative artefact per configuration tells us whether a build ran.
function(_bw_mpp_present cfg out_var)
    set(suffix "")
    if(cfg STREQUAL "Debug")
        set(suffix "d")
    endif()
    foreach(stem MassivePolyPusher MppMesh MppHelper MppProgram MppData Utils)
        if(NOT EXISTS "${BW_MPP_SOURCE_DIR}/build/lib/${cfg}/${stem}${suffix}.lib")
            set(${out_var} FALSE PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out_var} TRUE PARENT_SCOPE)
endfunction()

function(bw_ensure_mpp)
    if(NOT EXISTS "${BW_MPP_SOURCE_DIR}/mpp/include")
        message(FATAL_ERROR
            "ext/massive-poly-pusher is empty. Clone with --recurse-submodules, "
            "or run: git submodule update --init --recursive")
    endif()

    foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
        _bw_mpp_present("${cfg}" present)
        if(present)
            continue()
        endif()
        if(NOT BW_BUILD_MPP)
            message(FATAL_ERROR
                "MassivePolyPusher libraries for ${cfg} are missing and BW_BUILD_MPP is OFF. "
                "Build ext/massive-poly-pusher yourself first.")
        endif()

        if(NOT EXISTS "${BW_MPP_SOURCE_DIR}/build/CMakeCache.txt")
            message(STATUS "Configuring MassivePolyPusher (this takes a couple of minutes)")
            execute_process(
                COMMAND "${CMAKE_COMMAND}" -S "${BW_MPP_SOURCE_DIR}" -B "${BW_MPP_SOURCE_DIR}/build"
                        -G "${CMAKE_GENERATOR}" -A x64
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
            if(NOT rc EQUAL 0)
                message(FATAL_ERROR "Failed to configure MassivePolyPusher:\n${out}")
            endif()
        endif()

        message(STATUS "Building MassivePolyPusher (${cfg}) - this takes several minutes")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" --build "${BW_MPP_SOURCE_DIR}/build" --config "${cfg}"
                    --parallel
                    --target MppHelper MppMesh MppProgram MppData MppAppSupport MppResourceParsers
            RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
        if(NOT rc EQUAL 0)
            message(FATAL_ERROR "Failed to build MassivePolyPusher (${cfg}):\n${out}")
        endif()
    endforeach()
endfunction()
