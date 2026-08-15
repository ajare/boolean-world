# The submodules keep their own Visual Studio solutions and are never modified
# by this build. They only have to exist as import libraries before anything
# here links, so build them with MSBuild if they are missing.
#
# Set BW_BUILD_SUBMODULES=OFF to manage them yourself.

option(BW_BUILD_SUBMODULES "Build ext/Utils and ext/MassivePolyPusher with MSBuild if not already built" ON)

# Order matters: MassivePolyPusher links its own nested ext/utils, not the
# top-level one, so that copy has to come first.
set(BW_SUBMODULE_SOLUTIONS
    "${BW_ROOT}/ext/MassivePolyPusher/ext/utils/build/vs2026/Utils.sln"
    "${BW_ROOT}/ext/Utils/build/vs2026/Utils.sln"
    "${BW_ROOT}/ext/MassivePolyPusher/build/vs2026/MassivePolyPusher.sln")

# One representative artefact per configuration tells us whether a build ran.
function(_bw_submodules_present cfg out_var)
    set(suffix "")
    if(cfg STREQUAL "Debug")
        set(suffix "d")
    endif()
    set(probes
        "${BW_ROOT}/ext/Utils/build/vs2026/lib/x64/${cfg}/Utils${suffix}.lib"
        "${BW_ROOT}/ext/MassivePolyPusher/mpp/build/vs2026/lib/x64/${cfg}/MassivePolyPusher${suffix}.lib"
        "${BW_ROOT}/ext/MassivePolyPusher/mpp-mesh/build/vs2026/lib/x64/${cfg}/MppMesh${suffix}.lib"
        "${BW_ROOT}/ext/MassivePolyPusher/mpp-helper/build/vs2026/lib/x64/${cfg}/MppHelper${suffix}.lib"
        "${BW_ROOT}/ext/MassivePolyPusher/mpp-program/build/vs2026/lib/x64/${cfg}/MppProgram${suffix}.lib")
    foreach(p ${probes})
        if(NOT EXISTS "${p}")
            set(${out_var} FALSE PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out_var} TRUE PARENT_SCOPE)
endfunction()

function(bw_ensure_submodules)
    if(NOT EXISTS "${BW_ROOT}/ext/MassivePolyPusher/mpp/include")
        message(FATAL_ERROR
            "ext/MassivePolyPusher is empty. Clone with --recurse-submodules, "
            "or run: git submodule update --init --recursive")
    endif()

    find_program(BW_MSBUILD NAMES MSBuild.exe
        HINTS "$ENV{ProgramFiles}/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin"
              "$ENV{ProgramFiles}/Microsoft Visual Studio/18/Professional/MSBuild/Current/Bin"
              "$ENV{ProgramFiles}/Microsoft Visual Studio/18/Enterprise/MSBuild/Current/Bin")

    foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
        # Profiling has no submodule counterpart; it links the Release libraries.
        if(cfg STREQUAL "Profiling")
            continue()
        endif()
        _bw_submodules_present("${cfg}" present)
        if(present)
            continue()
        endif()
        if(NOT BW_BUILD_SUBMODULES)
            message(FATAL_ERROR
                "Submodule libraries for ${cfg}|x64 are missing and "
                "BW_BUILD_SUBMODULES is OFF. Build them with their own solutions first.")
        endif()
        if(NOT BW_MSBUILD)
            message(FATAL_ERROR
                "Submodule libraries for ${cfg}|x64 are missing and MSBuild was not found. "
                "Build the solutions under ext/ manually, or pass -DBW_MSBUILD=<path to MSBuild.exe>.")
        endif()
        foreach(sln ${BW_SUBMODULE_SOLUTIONS})
            get_filename_component(_n "${sln}" NAME)
            message(STATUS "Building submodule ${_n} (${cfg}|x64)")
            execute_process(
                COMMAND "${BW_MSBUILD}" "${sln}"
                        /t:Build "/p:Configuration=${cfg}" /p:Platform=x64
                        /m /v:minimal /nologo
                RESULT_VARIABLE rc
                OUTPUT_VARIABLE out
                ERROR_VARIABLE  out)
            if(NOT rc EQUAL 0)
                message(FATAL_ERROR "Failed to build ${_n} (${cfg}|x64):\n${out}")
            endif()
        endforeach()
    endforeach()
endfunction()
