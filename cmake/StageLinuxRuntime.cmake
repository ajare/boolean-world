# Stage the non-system shared libraries required by Launcher and the game
# library. Game code is loaded with dlopen(), so it must be supplied to
# GET_RUNTIME_DEPENDENCIES explicitly rather than being discovered from the
# Launcher's ELF dependency table.
foreach(required_var EXECUTABLE GAME_LIBRARY DESTINATION PROJECT_ROOT READELF)
    if(NOT DEFINED ${required_var})
        message(FATAL_ERROR "StageLinuxRuntime.cmake requires ${required_var}")
    endif()
endforeach()

# Remove libraries left by an earlier staging pass before dependency scanning.
# Otherwise $ORIGIN and build-tree RUNPATH entries can resolve the same SONAME
# to two files and CMake correctly reports an ambiguous dependency.
file(GLOB staged_libraries "${DESTINATION}/*.so*")
if(staged_libraries)
    file(REMOVE ${staged_libraries})
endif()

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${EXECUTABLE}"
    LIBRARIES "${GAME_LIBRARY}"
    RESOLVED_DEPENDENCIES_VAR resolved_dependencies
    UNRESOLVED_DEPENDENCIES_VAR unresolved_dependencies)

if(unresolved_dependencies)
    list(JOIN unresolved_dependencies ", " unresolved_list)
    message(FATAL_ERROR
        "Could not resolve Launcher runtime dependencies: ${unresolved_list}")
endif()

# GET_RUNTIME_DEPENDENCIES returns the dependencies of a supplied library, not
# the library itself. Include the dlopen() target in the staged set as well.
list(APPEND resolved_dependencies "${GAME_LIBRARY}")

cmake_path(SET project_root NORMALIZE "${PROJECT_ROOT}")
foreach(dependency IN LISTS resolved_dependencies)
    cmake_path(IS_PREFIX project_root "${dependency}" NORMALIZE
               is_project_dependency)
    if(NOT is_project_dependency)
        continue()
    endif()

    get_filename_component(dependency_name "${dependency}" NAME)
    set(staged_dependency "${DESTINATION}/${dependency_name}")
    file(COPY_FILE "${dependency}" "${staged_dependency}"
         ONLY_IF_DIFFERENT)

    # Standalone dependency builds contain absolute RUNPATH entries pointing
    # back into their build trees. Rewrite only the staged copy so all of its
    # indirect dependencies are resolved beside Launcher as well.
    execute_process(
        COMMAND "${READELF}" -d "${staged_dependency}"
        RESULT_VARIABLE readelf_result
        OUTPUT_VARIABLE dynamic_section
        ERROR_VARIABLE readelf_error)
    if(NOT readelf_result EQUAL 0)
        message(FATAL_ERROR
            "Could not inspect ${staged_dependency}: ${readelf_error}")
    endif()
    string(REGEX MATCH "Library (runpath|rpath): \\[([^]]*)\\]"
           rpath_match "${dynamic_section}")
    if(rpath_match)
        file(RPATH_CHANGE FILE "${staged_dependency}"
             OLD_RPATH "${CMAKE_MATCH_2}" NEW_RPATH "$ORIGIN")
    endif()
endforeach()
