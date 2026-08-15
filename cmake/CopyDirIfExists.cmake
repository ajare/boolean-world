# Copy a directory's contents if it exists, otherwise do nothing.
#
# Run in script mode from a POST_BUILD step, so both the $<CONFIG> in the path
# and the directory's existence are resolved at build time:
#
#   ${CMAKE_COMMAND} -DSRC=... -DDST=... -P cmake/CopyDirIfExists.cmake
#
# cmake -E copy_directory fails outright on a missing source, which is no good
# for per-machine support directories that most machines will not have.

if(NOT DEFINED SRC OR NOT DEFINED DST)
    message(FATAL_ERROR "CopyDirIfExists.cmake requires -DSRC= and -DDST=")
endif()

if(IS_DIRECTORY "${SRC}")
    file(GLOB _entries "${SRC}/*")
    if(_entries)
        file(COPY ${_entries} DESTINATION "${DST}")
        message(STATUS "Copied support files from ${SRC}")
    endif()
endif()
