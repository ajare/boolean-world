# Invoked by bw_deploy_runtime_dlls(). The pipe-delimited input avoids CMake
# expanding a generated DLL list into separate -D command-line arguments.
if(NOT DEFINED BW_RUNTIME_DLL_DESTINATION)
    message(FATAL_ERROR "BW_RUNTIME_DLL_DESTINATION is required")
endif()

if(NOT DEFINED BW_RUNTIME_DLLS OR BW_RUNTIME_DLLS STREQUAL "")
    return()
endif()

string(REPLACE "|" ";" runtime_dlls "${BW_RUNTIME_DLLS}")
foreach(runtime_dll IN LISTS runtime_dlls)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${runtime_dll}" "${BW_RUNTIME_DLL_DESTINATION}"
        RESULT_VARIABLE copy_result)
    if(NOT copy_result EQUAL 0)
        message(FATAL_ERROR
            "Could not stage runtime DLL '${runtime_dll}' (exit ${copy_result})")
    endif()
endforeach()
