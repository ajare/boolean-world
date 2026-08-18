# Shared helpers for the BooleanWorld CMake build.

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
    set_target_properties(${tgt} PROPERTIES
        DEBUG_POSTFIX "d"
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug,MemCheck>:Debug>DLL")
    target_compile_options(${tgt} PRIVATE /MP)
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
    foreach(tgt ${ARGN})
        target_compile_options(${tgt} PRIVATE /sdl)
    endforeach()
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
    add_custom_command(TARGET ${tgt} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_RUNTIME_DLLS:${tgt}>" "$<TARGET_FILE_DIR:${tgt}>"
        COMMAND_EXPAND_LISTS
        VERBATIM
        COMMENT "Staging runtime DLLs for ${tgt}")
endfunction()

# bw_deploy_vendor_dlls(<target>)
#
# vendor/bin holds DLLs (the FMOD family) whose import libraries are
# static stubs, so they never show up in TARGET_RUNTIME_DLLS. Copy the whole
# per-configuration directory, which is what the old .bat files did.
function(bw_deploy_vendor_dlls tgt)
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
