# Shared helpers for the BooleanWorld CMake build.

# bw_target_defaults(<target>)
#
# Settings every project shared: multi-processor compilation and the
# per-configuration name suffixes (Debug "d", Profiling "p") the original
# build used.
#
# /sdl is deliberately NOT here. Six of the Willpower modules had
# SDLCheck off, and enabling it changes codegen - it emits __autoclassinit2
# helpers that show up as extra exported symbols. Use bw_enable_sdl_checks
# on the targets that had it.
function(bw_target_defaults tgt)
    set_target_properties(${tgt} PROPERTIES
        DEBUG_POSTFIX     "d"
        PROFILING_POSTFIX "p"
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    target_compile_options(${tgt} PRIVATE /MP)
    # _DEBUG / NDEBUG were spelled out per configuration in every vcxproj.
    target_compile_definitions(${tgt} PRIVATE
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)
endfunction()

# bw_output_dirs(<target> <runtime-dir> [archive-dir])
#
# Places build products exactly where the original .vcxproj put them. Paths are
# relative to the current source directory; "$<CONFIG>" is appended by the
# caller's pattern. If archive-dir is omitted the import library sits beside
# the runtime output.
function(bw_output_dirs tgt runtime)
    set(archive "${runtime}")
    if(ARGC GREATER 2)
        set(archive "${ARGV2}")
    endif()
    set_target_properties(${tgt} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${runtime}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${runtime}"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${archive}"
        PDB_OUTPUT_DIRECTORY     "${CMAKE_CURRENT_SOURCE_DIR}/${runtime}")
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
# The executables kept the same file name in every configuration; only the
# Profiling suffix applied to them.
function(bw_no_postfix)
    foreach(tgt ${ARGN})
        set_target_properties(${tgt} PROPERTIES DEBUG_POSTFIX "")
    endforeach()
endfunction()

# bw_add_version_resource(<target>)
#
# Compiles build/version/Version.rc into the target, embedding the Windows
# version-info block (FileVersion, ProductName, ...). The .rc includes
# Version.h from the same directory, so that directory has to be on the
# resource compiler's include path.
function(bw_add_version_resource tgt)
    set(rc "${CMAKE_CURRENT_SOURCE_DIR}/build/version/Version.rc")
    if(NOT EXISTS "${rc}")
        message(FATAL_ERROR "${tgt}: expected a version resource at ${rc}")
    endif()
    target_sources(${tgt} PRIVATE "${rc}")
    target_include_directories(${tgt} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/build/version")
    source_group("Resource Files" FILES "${rc}")
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
# vendor/bin holds DLLs (FMOD, FreeImage, GLEW, SDL) whose import libraries are
# static stubs, so they never show up in TARGET_RUNTIME_DLLS. Copy the whole
# per-configuration directory, which is what the old .bat files did.
function(bw_deploy_vendor_dlls tgt)
    add_custom_command(TARGET ${tgt} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${BW_VENDOR_BIN}/$<IF:$<CONFIG:Debug>,Debug,Release>"
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
