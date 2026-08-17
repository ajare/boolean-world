# Willpower and its dependencies keep their own CMake build and are consumed by
# this project as prebuilt libraries. Configure and build Willpower on demand
# before the imported targets are declared.
#
# Set BW_BUILD_WILLPOWER=OFF to manage the submodule build yourself.

option(BW_BUILD_WILLPOWER
    "Configure and build Willpower if its libraries have not been built" ON)

set(BW_WILLPOWER_SOURCE_DIR "${BW_ROOT}/ext/willpower")
set(BW_WILLPOWER_BUILD_DIR "${BW_WILLPOWER_SOURCE_DIR}/build")
set(BW_MPP_SOURCE_DIR "${BW_WILLPOWER_SOURCE_DIR}/ext/massive-poly-pusher")
set(BW_MPP_BUILD_DIR "${BW_WILLPOWER_BUILD_DIR}/_deps/massive-poly-pusher-build")

function(_bw_willpower_present cfg out_var)
    set(suffix "")
    if(cfg STREQUAL "Debug")
        set(suffix "d")
    endif()

    foreach(target
            Willpower.Common
            Willpower.Geometry
            Willpower.Collide
            Willpower.Application
            WillPower.Viz)
        if(NOT EXISTS
                "${BW_WILLPOWER_BUILD_DIR}/lib/${cfg}/${target}/${target}${suffix}.lib")
            set(${out_var} FALSE PARENT_SCOPE)
            return()
        endif()
    endforeach()

    foreach(file MppAppSupport SDL3)
        if(NOT EXISTS "${BW_MPP_BUILD_DIR}/lib/${cfg}/${file}${suffix}.lib")
            set(${out_var} FALSE PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out_var} TRUE PARENT_SCOPE)
endfunction()

function(bw_ensure_willpower)
    if(NOT EXISTS "${BW_WILLPOWER_SOURCE_DIR}/CMakeLists.txt" OR
       NOT EXISTS "${BW_MPP_SOURCE_DIR}/mpp/include")
        message(FATAL_ERROR
            "Willpower or one of its nested dependencies is empty. "
            "Clone with --recurse-submodules, or run: "
            "git submodule update --init --recursive")
    endif()

    foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
        _bw_willpower_present("${cfg}" present)
        if(present)
            continue()
        endif()
        if(NOT BW_BUILD_WILLPOWER)
            message(FATAL_ERROR
                "Willpower libraries for ${cfg} are missing and "
                "BW_BUILD_WILLPOWER is OFF. Build ext/willpower yourself first.")
        endif()

        if(NOT EXISTS "${BW_WILLPOWER_BUILD_DIR}/CMakeCache.txt")
            message(STATUS "Configuring Willpower")
            execute_process(
                COMMAND "${CMAKE_COMMAND}"
                        -S "${BW_WILLPOWER_SOURCE_DIR}"
                        -B "${BW_WILLPOWER_BUILD_DIR}"
                        -G "${CMAKE_GENERATOR}"
                        -A x64
                        -DBUILD_TESTING=OFF
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
            if(NOT rc EQUAL 0)
                message(FATAL_ERROR "Failed to configure Willpower:\n${out}")
            endif()
        endif()

        message(STATUS "Building Willpower (${cfg}) - this may take several minutes")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" --build "${BW_WILLPOWER_BUILD_DIR}"
                    --config "${cfg}" --parallel
                    --target Willpower.Common Willpower.Geometry
                             Willpower.Collide Willpower.Application WillPower.Viz
            RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
        if(NOT rc EQUAL 0)
            message(FATAL_ERROR "Failed to build Willpower (${cfg}):\n${out}")
        endif()

        # BooleanWorld also consumes MppAppSupport and SDL directly. They are
        # outside Willpower's target dependency graph, so build them explicitly
        # in the same standalone dependency tree.
        execute_process(
            COMMAND "${CMAKE_COMMAND}" --build "${BW_MPP_BUILD_DIR}"
                    --config "${cfg}" --parallel --target MppAppSupport
            RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
        if(NOT rc EQUAL 0)
            message(FATAL_ERROR
                "Failed to build MassivePolyPusher support (${cfg}):\n${out}")
        endif()
    endforeach()
endfunction()
