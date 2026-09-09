# Shared helpers for the BooleanWorld CMake build.

# add_test(NAME <name> COMMAND <command> [arguments...])
#
# On Windows, run every CTest test beneath a statically linked launcher that
# disables inherited system error UI before the test image is loaded. Putting
# NonInteractiveErrorMode.cpp in each executable is still useful for CRT
# reports, but cannot suppress the missing-DLL dialog because that failure
# occurs before any code in the test executable can run.
#
# Tests whose COMMAND names a CMake executable target also get their transitive
# runtime DLLs staged automatically. Keeping both policies here means a newly
# registered test is non-interactive and deployable by default rather than
# relying on every caller to remember separate setup.
function(add_test)
    cmake_parse_arguments(PARSE_ARGV 0 _bw_test "COMMAND_EXPAND_LISTS"
        "NAME;WORKING_DIRECTORY" "COMMAND;CONFIGURATIONS")
    if(_bw_test_UNPARSED_ARGUMENTS OR NOT _bw_test_NAME OR NOT _bw_test_COMMAND)
        message(FATAL_ERROR "Unsupported add_test arguments: ${ARGV}")
    endif()

    if(NOT WIN32 OR NOT TARGET boolean_world_test_launcher)
        _add_test(${ARGV})
        set_tests_properties("${_bw_test_NAME}" PROPERTIES
            TIMEOUT "${BW_TEST_TIMEOUT_SECONDS}")
        return()
    endif()

    list(GET _bw_test_COMMAND 0 _bw_test_command)
    if(TARGET "${_bw_test_command}")
        add_dependencies("${_bw_test_command}" boolean_world_test_launcher)
        bw_deploy_runtime_dlls("${_bw_test_command}")
        list(REMOVE_AT _bw_test_COMMAND 0)
        list(PREPEND _bw_test_COMMAND "$<TARGET_FILE:${_bw_test_command}>")
    endif()

    set(_bw_test_options)
    if(_bw_test_CONFIGURATIONS)
        list(APPEND _bw_test_options CONFIGURATIONS ${_bw_test_CONFIGURATIONS})
    endif()
    if(_bw_test_WORKING_DIRECTORY)
        list(APPEND _bw_test_options WORKING_DIRECTORY "${_bw_test_WORKING_DIRECTORY}")
    endif()
    if(_bw_test_COMMAND_EXPAND_LISTS)
        list(APPEND _bw_test_options COMMAND_EXPAND_LISTS)
    endif()
    _add_test(NAME "${_bw_test_NAME}"
        COMMAND "$<TARGET_FILE:boolean_world_test_launcher>" ${_bw_test_COMMAND}
        ${_bw_test_options})
    set_tests_properties("${_bw_test_NAME}" PROPERTIES
        TIMEOUT "${BW_TEST_TIMEOUT_SECONDS}")
endfunction()

# bw_add_header_filter(<target> [include-dir...])
#
# Add first-party headers to a target so Visual Studio emits a "Header Files"
# filter in the generated project. With no directory arguments, use the
# target's conventional include directory.
function(bw_add_header_filter tgt)
    if(ARGC EQUAL 1)
        set(header_dirs "${CMAKE_CURRENT_SOURCE_DIR}/include")
    else()
        set(header_dirs ${ARGN})
    endif()

    foreach(header_dir IN LISTS header_dirs)
        if(NOT IS_DIRECTORY "${header_dir}")
            continue()
        endif()

        file(GLOB_RECURSE headers CONFIGURE_DEPENDS
            "${header_dir}/*.h"
            "${header_dir}/*.hpp"
            "${header_dir}/*.inl")
        if(headers)
            target_sources(${tgt} PRIVATE ${headers})
            source_group(TREE "${header_dir}" PREFIX "Header Files" FILES ${headers})
        endif()
    endforeach()
endfunction()

# bw_target_defaults(<target>)
#
# Settings every project shared: multi-processor compilation and the Debug "d"
# name suffix the original build used.
#
# /sdl is deliberately NOT here. Six of the Willpower modules had
# SDLCheck off, and enabling it changes codegen - it emits __autoclassinit2
# helpers that show up as extra exported symbols. Use bw_enable_sdl_checks
# on the targets that had it.
function(bw_target_defaults tgt)
    set_target_properties(${tgt} PROPERTIES DEBUG_POSTFIX "d")
    get_target_property(_bw_type ${tgt} TYPE)
    if(_bw_type STREQUAL "EXECUTABLE")
        # Keep applications and test runners non-interactive when Windows or
        # the debug CRT reports an error. The source is a no-op off Windows.
        target_sources(${tgt} PRIVATE
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/NonInteractiveErrorMode.cpp")
    endif()
    if(MSVC)
        set_target_properties(${tgt} PROPERTIES
            MSVC_RUNTIME_LIBRARY "${BW_MSVC_RUNTIME_LIBRARY}")
        target_compile_options(${tgt} PRIVATE /MP)
    endif()
    # _DEBUG / NDEBUG were spelled out per configuration in every vcxproj.
    # MemCheck is a Debug build (see the top-level CMakeLists.txt), so it
    # takes the _DEBUG branch too.
    target_compile_definitions(${tgt} PRIVATE
        $<$<CONFIG:Debug,MemCheck>:_DEBUG>
        $<$<NOT:$<CONFIG:Debug,MemCheck>>:NDEBUG>)
endfunction()

# bw_output_dirs(<target>)
#
# Keeps every generated binary beneath the CMake build tree. Per-target
# directories prevent post-build dependency staging for one executable from
# polluting or racing another target's output.
function(bw_output_dirs tgt)
    set(runtime "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/${tgt}")
    set(archive "${CMAKE_BINARY_DIR}/lib/$<CONFIG>/${tgt}")
    set_target_properties(${tgt} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${runtime}"
        LIBRARY_OUTPUT_DIRECTORY "${runtime}"
        ARCHIVE_OUTPUT_DIRECTORY "${archive}"
        PDB_OUTPUT_DIRECTORY     "${runtime}")

    get_target_property(_bw_type ${tgt} TYPE)
    if(_bw_type STREQUAL "EXECUTABLE")
        bw_deploy_asan_runtime(${tgt})
    endif()
endfunction()

# bw_deploy_asan_runtime(<target>)
#
# MemCheck compiles with /fsanitize=address, which needs its runtime DLL
# next to the executable - and its PDB alongside that, so ASan frames in a
# call stack resolve to symbols instead of raw addresses. No-op outside the
# MemCheck configuration: the $<$<CONFIG:MemCheck>:...> generator expression
# drops the COMMAND when it evaluates empty, and the same generator
# expression on COMMENT keeps the build step silent for Debug/Release too.
function(bw_deploy_asan_runtime tgt)
    if(NOT MSVC)
        return()
    endif()
    get_filename_component(msvc_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    add_custom_command(TARGET ${tgt} POST_BUILD
        COMMAND "$<$<CONFIG:MemCheck>:${CMAKE_COMMAND}>" -E copy_if_different
                "${msvc_bin_dir}/clang_rt.asan_dynamic-x86_64.dll"
                "${msvc_bin_dir}/clang_rt.asan_dynamic-x86_64.pdb"
                "$<TARGET_FILE_DIR:${tgt}>"
        VERBATIM
        COMMENT "$<$<CONFIG:MemCheck>:Staging AddressSanitizer runtime for ${tgt} (MemCheck)>")
endfunction()

# bw_enable_sdl_checks(<target>...)
#
# /sdl, matching <SDLCheck>true</SDLCheck> in the original .vcxproj.
function(bw_enable_sdl_checks)
    if(MSVC)
        foreach(tgt ${ARGN})
            target_compile_options(${tgt} PRIVATE /sdl)
        endforeach()
    endif()
endfunction()

# bw_no_postfix(<target>...)
#
# The executables kept the same file name in every configuration.
function(bw_no_postfix)
    foreach(tgt ${ARGN})
        set_target_properties(${tgt} PROPERTIES DEBUG_POSTFIX "")
    endforeach()
endfunction()

# bw_deploy_runtime_dlls(<target>)
#
# Stage every shared library the target needs next to the executable. This
# replaces CopyWillpowerBinaries.bat / CopySupportFiles.bat: CMake already
# knows the full transitive set, so nothing has to be listed by hand.
function(bw_deploy_runtime_dlls tgt)
    if(NOT WIN32)
        return()
    endif()

    # add_test() applies this automatically, while existing application and
    # explicit test setup may apply it earlier. Only attach one post-build
    # command in either case.
    get_property(_bw_runtime_dlls_configured TARGET ${tgt}
        PROPERTY BW_RUNTIME_DLLS_CONFIGURED SET)
    if(_bw_runtime_dlls_configured)
        return()
    endif()
    set_property(TARGET ${tgt} PROPERTY BW_RUNTIME_DLLS_CONFIGURED TRUE)

    # A test with no shared-library dependencies has an empty runtime DLL list.
    # Route staging through a script so that case is a successful no-op. A pipe
    # is safe as the list separator because Windows paths cannot contain it.
    add_custom_command(TARGET ${tgt} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                "-DBW_RUNTIME_DLLS=$<JOIN:$<TARGET_RUNTIME_DLLS:${tgt}>,|>"
                "-DBW_RUNTIME_DLL_DESTINATION=$<TARGET_FILE_DIR:${tgt}>"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/DeployRuntimeDlls.cmake"
        VERBATIM
        COMMENT "Staging runtime DLLs for ${tgt}")
endfunction()

# bw_deploy_vendor_dlls(<target>)
#
# vendor/bin holds DLLs (the FMOD family) whose import libraries are
# static stubs, so they never show up in TARGET_RUNTIME_DLLS. Copy the whole
# per-configuration directory, which is what the old .bat files did.
function(bw_deploy_vendor_dlls tgt)
    if(NOT WIN32)
        return()
    endif()

    get_property(_bw_vendor_dlls_configured TARGET ${tgt}
        PROPERTY BW_VENDOR_DLLS_CONFIGURED SET)
    if(_bw_vendor_dlls_configured)
        return()
    endif()
    set_property(TARGET ${tgt} PROPERTY BW_VENDOR_DLLS_CONFIGURED TRUE)

    add_custom_command(TARGET ${tgt} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${BW_VENDOR_BIN}/$<IF:$<CONFIG:Debug,MemCheck>,Debug,Release>"
                "$<TARGET_FILE_DIR:${tgt}>"
        VERBATIM
        COMMENT "Staging vendor DLLs for ${tgt}")
endfunction()

# bw_deploy_directory(<target> <dir> [subdir])
#
# Copy a resources directory next to the executable after every build.
function(bw_deploy_directory tgt dir)
    if(NOT IS_DIRECTORY "${dir}")
        return()
    endif()
    set(dest "$<TARGET_FILE_DIR:${tgt}>")
    if(ARGC GREATER 2)
        set(dest "${dest}/${ARGV2}")
    endif()
    add_custom_command(TARGET ${tgt} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${dir}" "${dest}"
        VERBATIM
        COMMENT "Staging ${dir} for ${tgt}")
endfunction()
