# Stage libraries loaded dynamically by FMOD. They do not all appear in ELF
# dependency metadata, so StageLinuxRuntime cannot discover them.
foreach(required_var
        FMOD_CORE_DIR FMOD_STUDIO_DIR STEAM_AUDIO_LIBRARY
        STEAM_AUDIO_FMOD_PLUGIN DESTINATION)
    if(NOT DEFINED ${required_var})
        message(FATAL_ERROR "StageLinuxAudio.cmake requires ${required_var}")
    endif()
endforeach()
foreach(required_path
        FMOD_CORE_DIR FMOD_STUDIO_DIR STEAM_AUDIO_LIBRARY
        STEAM_AUDIO_FMOD_PLUGIN)
    if(NOT EXISTS "${${required_path}}")
        message(FATAL_ERROR
            "StageLinuxAudio.cmake requires an existing ${required_path}")
    endif()
endforeach()
file(MAKE_DIRECTORY "${DESTINATION}")

# Copy the versioned FMOD ELF files as well as their development symlinks:
# DT_NEEDED names libfmod.so.<major>, not the unversioned libfmod.so.
file(COPY "${FMOD_CORE_DIR}/" DESTINATION "${DESTINATION}")
if(NOT FMOD_STUDIO_DIR STREQUAL FMOD_CORE_DIR)
    file(COPY "${FMOD_STUDIO_DIR}/" DESTINATION "${DESTINATION}")
endif()
file(COPY_FILE "${STEAM_AUDIO_LIBRARY}"
     "${DESTINATION}/libphonon.so" ONLY_IF_DIFFERENT)
file(COPY_FILE "${STEAM_AUDIO_FMOD_PLUGIN}"
     "${DESTINATION}/libphonon_fmod.so" ONLY_IF_DIFFERENT)
