function(run_resource_manager expected_result description)
    execute_process(
        COMMAND "${RESOURCE_MANAGER}" --ini "${INI}" ${ARGN}
        RESULT_VARIABLE actual_result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    if(NOT actual_result EQUAL expected_result)
        message(FATAL_ERROR
            "${description}: resource-manager returned ${actual_result}, expected ${expected_result}\n"
            "stdout:\n${output}\nstderr:\n${error}")
    endif()
endfunction()

run_resource_manager(0 "bundle verification" --verify-schemas)
run_resource_manager(0 "BooleanWorld Resources.yaml"
    --validate "${APPLICATION_MANIFEST}"
    --base-directory "${APPLICATION_BASE_DIRECTORY}")
run_resource_manager(0 "valid application Resource Types"
    --validate "${FIXTURE_DIRECTORY}/valid-application-resources.yaml"
    --base-directory "${FIXTURE_DIRECTORY}")

foreach(invalid_fixture
        invalid-map-default.yaml
        invalid-map-tiled.yaml
        invalid-map-boolean-world.yaml
        invalid-proto-entity.yaml
        invalid-lua-script.yaml
        invalid-proc-material.yaml
        invalid-embossing-catalog.yaml
        invalid-acoustic-catalog.yaml)
    run_resource_manager(4 "${invalid_fixture}"
        --validate "${FIXTURE_DIRECTORY}/${invalid_fixture}"
        --base-directory "${FIXTURE_DIRECTORY}")
endforeach()
