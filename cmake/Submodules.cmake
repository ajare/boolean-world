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

    # MemCheck reuses the Debug Willpower/MassivePolyPusher build (see
    # CMAKE_MAP_IMPORTED_CONFIG_MEMCHECK in the top-level CMakeLists.txt) -
    # Willpower's own build has no MemCheck configuration to pass here.
    set(_bw_underlying_configs "")
    foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
        if(cfg STREQUAL "MemCheck")
            list(APPEND _bw_underlying_configs "Debug")
        else()
            list(APPEND _bw_underlying_configs "${cfg}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _bw_underlying_configs)

    # Imported targets cannot express a build dependency on these standalone
    # trees. Track their checked-out revisions so a submodule update invalidates
    # otherwise-present prebuilt artifacts without rebuilding on every configure.
    execute_process(
        COMMAND git -C "${BW_WILLPOWER_SOURCE_DIR}" rev-parse HEAD
        RESULT_VARIABLE willpower_revision_rc
        OUTPUT_VARIABLE willpower_revision OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(
        COMMAND git -C "${BW_MPP_SOURCE_DIR}" rev-parse HEAD
        RESULT_VARIABLE mpp_revision_rc
        OUTPUT_VARIABLE mpp_revision OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(willpower_revision_rc EQUAL 0 AND mpp_revision_rc EQUAL 0)
        set(dependency_revision "${willpower_revision}\n${mpp_revision}\n")
    else()
        # A source export without Git metadata cannot prove that its artifacts
        # are current, so conservatively ask the native build tool every time.
        set(dependency_revision "")
    endif()

    foreach(cfg ${_bw_underlying_configs})
        _bw_willpower_present("${cfg}" present)
        set(revision_file
            "${BW_WILLPOWER_BUILD_DIR}/boolean-world-${cfg}-revision.txt")
        set(revisions_match FALSE)
        if(dependency_revision AND EXISTS "${revision_file}")
            file(READ "${revision_file}" built_revision)
            if(built_revision STREQUAL dependency_revision)
                set(revisions_match TRUE)
            endif()
        endif()

        if(NOT BW_BUILD_WILLPOWER)
            if(NOT present)
                message(FATAL_ERROR
                    "Willpower libraries for ${cfg} are missing and "
                    "BW_BUILD_WILLPOWER is OFF. Build ext/willpower yourself first.")
            endif()
            continue()
        endif()
        if(present AND revisions_match)
            continue()
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

        message(STATUS "Updating Willpower dependencies (${cfg}) - this may take several minutes")

        # Build MPP directly rather than relying on Willpower's ExternalProject
        # stamp. That stamp records only that the external build once completed;
        # it does not notice when the checked-out MPP submodule advances, which
        # can otherwise leave current headers paired with stale runtime DLLs.
        # MppAppSupport's dependencies include every MPP library BooleanWorld
        # consumes directly.
        execute_process(
            COMMAND "${CMAKE_COMMAND}" --build "${BW_MPP_BUILD_DIR}"
                    --config "${cfg}" --parallel --target MppAppSupport
            RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
        if(NOT rc EQUAL 0)
            message(FATAL_ERROR
                "Failed to build MassivePolyPusher support (${cfg}):\n${out}")
        endif()

        # Always ask the standalone build to update too. The native build tool
        # performs the incremental check, keeping its libraries in sync when
        # the Willpower submodule itself advances.
        execute_process(
            COMMAND "${CMAKE_COMMAND}" --build "${BW_WILLPOWER_BUILD_DIR}"
                    --config "${cfg}" --parallel
                    --target Willpower.Common Willpower.Geometry
                             Willpower.Collide Willpower.Application WillPower.Viz
            RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
        if(NOT rc EQUAL 0)
            message(FATAL_ERROR "Failed to build Willpower (${cfg}):\n${out}")
        endif()

        if(dependency_revision)
            file(WRITE "${revision_file}" "${dependency_revision}")
        endif()
    endforeach()
endfunction()
