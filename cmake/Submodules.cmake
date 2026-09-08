# Willpower and its dependencies keep their own CMake build and are consumed by
# this project as prebuilt libraries. Configure and build Willpower on demand
# before the imported targets are declared.
#
# Set BW_BUILD_WILLPOWER=OFF to manage the submodule build yourself.

option(BW_BUILD_WILLPOWER
    "Configure and build Willpower if its libraries have not been built" ON)

# The single switch for FMOD-backed audio. This drives WILLPOWER_ENABLE_FMOD in
# Willpower's own configure below - never set that option directly, since the
# two would otherwise be independently settable and could disagree with each
# other (see #381).
set(_bw_fmod_default ON)
option(BW_ENABLE_FMOD
    "Enable FMOD-backed audio (requires the FMOD Engine and Steam Audio SDKs)" ${_bw_fmod_default})
unset(_bw_fmod_default)

# SDK locations are cache entries so Linux builds can point at the official
# (redistribution-restricted) SDK installations without copying them into Git.
set(BW_FMOD_CORE_INCLUDE "${BW_ROOT}/vendor/include/fmod/core" CACHE PATH "FMOD core include directory")
set(BW_FMOD_STUDIO_INCLUDE "${BW_ROOT}/vendor/include/fmod/studio" CACHE PATH "FMOD Studio include directory")
if(WIN32)
    set(BW_FMOD_CORE_LIBRARY "${BW_ROOT}/vendor/lib/vs2026/x64/Release/fmod_vc.lib" CACHE FILEPATH "FMOD core library")
    set(BW_FMOD_STUDIO_LIBRARY "${BW_ROOT}/vendor/lib/vs2026/x64/Release/fmodstudio_vc.lib" CACHE FILEPATH "FMOD Studio library")
    set(BW_FMOD_CORE_DLL "${BW_ROOT}/vendor/bin/vs2026/x64/Release/fmod.dll" CACHE FILEPATH "FMOD core runtime")
    set(BW_FMOD_STUDIO_DLL "${BW_ROOT}/vendor/bin/vs2026/x64/Release/fmodstudio.dll" CACHE FILEPATH "FMOD Studio runtime")
else()
    set(_bw_linux_audio_dir "${BW_ROOT}/vendor/lib/linux/x64/Release")
    set(BW_FMOD_CORE_LIBRARY "${_bw_linux_audio_dir}/libfmod.so" CACHE FILEPATH "FMOD core library")
    set(BW_FMOD_STUDIO_LIBRARY "${_bw_linux_audio_dir}/libfmodstudio.so" CACHE FILEPATH "FMOD Studio library")
    set(BW_STEAM_AUDIO_LIBRARY "${_bw_linux_audio_dir}/libphonon.so" CACHE FILEPATH "Steam Audio core library")
    set(BW_STEAM_AUDIO_FMOD_PLUGIN "${_bw_linux_audio_dir}/libphonon_fmod.so" CACHE FILEPATH "Steam Audio FMOD plugin")
    unset(_bw_linux_audio_dir)
endif()

# Keep every generated solution distinguishable by platform while preserving a
# predictable relationship between the three nested checkouts. For example, a
# BooleanWorld build-windows tree consumes ext/willpower/build-windows and
# ext/willpower/ext/massive-poly-pusher/build-windows.
get_filename_component(BW_BUILD_DIR_NAME "${CMAKE_BINARY_DIR}" NAME)
if(NOT BW_BUILD_DIR_NAME)
    message(FATAL_ERROR "Could not determine the BooleanWorld build directory name")
endif()
set(BW_WILLPOWER_SOURCE_DIR "${BW_ROOT}/ext/willpower")
set(BW_WILLPOWER_BUILD_DIR
    "${BW_WILLPOWER_SOURCE_DIR}/${BW_BUILD_DIR_NAME}")
set(BW_MPP_SOURCE_DIR "${BW_WILLPOWER_SOURCE_DIR}/ext/massive-poly-pusher")
set(BW_MPP_BUILD_DIR "${BW_MPP_SOURCE_DIR}/${BW_BUILD_DIR_NAME}")
set(BW_MPP_OUTPUT_DIR "${BW_MPP_BUILD_DIR}")

function(_bw_willpower_present cfg out_var)
    if(WIN32)
        set(suffix "")
        if(cfg STREQUAL "Debug")
            set(suffix "d")
        endif()
        foreach(target Willpower.Common Willpower.Geometry Willpower.Wayfinder
                Willpower.Collide Willpower.Application WillPower.Viz)
            if(NOT EXISTS "${BW_WILLPOWER_BUILD_DIR}/lib/${cfg}/${target}/${target}${suffix}.lib")
                set(${out_var} FALSE PARENT_SCOPE)
                return()
            endif()
        endforeach()
        foreach(file MppAppSupport SDL3)
            if(NOT EXISTS "${BW_MPP_OUTPUT_DIR}/lib/${cfg}/${file}${suffix}.lib")
                set(${out_var} FALSE PARENT_SCOPE)
                return()
            endif()
        endforeach()
    else()
        foreach(target Willpower.Common Willpower.Geometry Willpower.Wayfinder
                Willpower.Collide Willpower.Application WillPower.Viz)
            if(NOT EXISTS "${BW_WILLPOWER_BUILD_DIR}/bin/${cfg}/${target}/lib${target}.so")
                set(${out_var} FALSE PARENT_SCOPE)
                return()
            endif()
        endforeach()
        if(NOT EXISTS "${BW_MPP_OUTPUT_DIR}/bin/${cfg}/libMassivePolyPusher.so")
            set(${out_var} FALSE PARENT_SCOPE)
            return()
        endif()
    endif()
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

    # MemCheck reuses the Debug Willpower/MassivePolyPusher builds (see the
    # CMAKE_MAP_IMPORTED_CONFIG_MEMCHECK mapping in the top-level
    # CMakeLists.txt). Shipping is built as a first-class configuration in
    # both dependency trees.
    set(_bw_underlying_configs "")
    if(CMAKE_CONFIGURATION_TYPES)
        set(_bw_requested_configs ${CMAKE_CONFIGURATION_TYPES})
    else()
        set(_bw_requested_configs ${CMAKE_BUILD_TYPE})
    endif()
    foreach(cfg ${_bw_requested_configs})
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
    # BW_ENABLE_FMOD (and where it points) is the single switch driving
    # Willpower's own WILLPOWER_ENABLE_FMOD. Folding it into the tracked state
    # forces a reconfigure whenever it changes, rather than silently keeping
    # whatever Willpower's cache already has - see #381.
    set(fmod_option_state
        "BW_ENABLE_FMOD=${BW_ENABLE_FMOD}\n${BW_FMOD_CORE_INCLUDE}\n${BW_FMOD_STUDIO_INCLUDE}\n${BW_FMOD_CORE_LIBRARY}\n${BW_FMOD_STUDIO_LIBRARY}\n${BW_FMOD_CORE_DLL}\n${BW_FMOD_STUDIO_DLL}\n${BW_STEAM_AUDIO_LIBRARY}\n${BW_STEAM_AUDIO_FMOD_PLUGIN}\n")

    if(willpower_revision_rc EQUAL 0 AND mpp_revision_rc EQUAL 0)
        set(dependency_revision "${willpower_revision}\n${mpp_revision}\n${fmod_option_state}")
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

        # Reconfigure whenever the cache is missing outright, or whenever
        # revisions_match is FALSE for a reason other than missing artifacts -
        # in particular, BW_ENABLE_FMOD changing. Gating this solely on cache
        # presence (as before) meant a flipped switch never reached Willpower's
        # cache on an existing checkout.
        if(NOT EXISTS "${BW_WILLPOWER_BUILD_DIR}/CMakeCache.txt" OR NOT revisions_match)
            message(STATUS "Configuring Willpower")
            set(_bw_configure_command
                "${CMAKE_COMMAND}" -S "${BW_WILLPOWER_SOURCE_DIR}"
                -B "${BW_WILLPOWER_BUILD_DIR}" -G "${CMAKE_GENERATOR}"
                -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=${cfg}
                -DWILLPOWER_ENABLE_FMOD=${BW_ENABLE_FMOD})
            # -A must only be passed the first time a cache is created: passing
            # it again on a reconfigure conflicts if the platform was implicit
            # (empty CMAKE_GENERATOR_PLATFORM) rather than explicitly recorded,
            # and CMake refuses to proceed. Updating -D cache variables alone
            # does not need it - the generator platform is already fixed.
            if(WIN32 AND NOT EXISTS "${BW_WILLPOWER_BUILD_DIR}/CMakeCache.txt")
                list(APPEND _bw_configure_command -A x64)
            endif()
            if(BW_ENABLE_FMOD)
                list(APPEND _bw_configure_command
                    -DWILLPOWER_FMOD_CORE_INCLUDE=${BW_FMOD_CORE_INCLUDE}
                    -DWILLPOWER_FMOD_STUDIO_INCLUDE=${BW_FMOD_STUDIO_INCLUDE}
                    -DWILLPOWER_FMOD_CORE_LIBRARY=${BW_FMOD_CORE_LIBRARY}
                    -DWILLPOWER_FMOD_STUDIO_LIBRARY=${BW_FMOD_STUDIO_LIBRARY})
                if(WIN32)
                    list(APPEND _bw_configure_command
                        -DWILLPOWER_FMOD_CORE_DLL=${BW_FMOD_CORE_DLL}
                        -DWILLPOWER_FMOD_STUDIO_DLL=${BW_FMOD_STUDIO_DLL})
                endif()
            endif()
            execute_process(
                COMMAND ${_bw_configure_command}
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
                             Willpower.Wayfinder Willpower.Collide
                             Willpower.Application WillPower.Viz
            RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
        if(NOT rc EQUAL 0)
            message(FATAL_ERROR "Failed to build Willpower (${cfg}):\n${out}")
        endif()

        if(dependency_revision)
            file(WRITE "${revision_file}" "${dependency_revision}")
        endif()
    endforeach()
endfunction()
