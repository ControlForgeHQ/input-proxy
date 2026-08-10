function(assert_cli expected_result expected_pattern)
    execute_process(
        COMMAND "${INPUT_PROXY}" ${ARGN}
        RESULT_VARIABLE actual_result
        OUTPUT_VARIABLE standard_output
        ERROR_VARIABLE standard_error
    )

    string(CONCAT output "${standard_output}" "${standard_error}")

    if(expected_result STREQUAL "success")
        if(NOT actual_result EQUAL 0)
            message(FATAL_ERROR "Command unexpectedly failed: ${ARGN}\n${output}")
        endif()
    elseif(actual_result EQUAL 0)
        message(FATAL_ERROR "Command unexpectedly succeeded: ${ARGN}\n${output}")
    endif()

    if(NOT output MATCHES "${expected_pattern}")
        message(
            FATAL_ERROR
            "Command output did not match '${expected_pattern}': ${ARGN}\n${output}"
        )
    endif()
endfunction()

assert_cli(success "Usage:" --help)
assert_cli(success "input-proxy [0-9]+\\.[0-9]+\\.[0-9]+" --version)
assert_cli(failure "missing command.*Usage:")
assert_cli(failure "unknown command 'bogus'.*Usage:" bogus)
assert_cli(failure "invalid run arguments.*Usage:" run)
assert_cli(failure "command 'list' is not yet implemented" list)
assert_cli(failure "command 'inspect' is not yet implemented" inspect /dev/input/event0)
assert_cli(failure "unknown command '--source'.*Usage:" --source /dev/input/event0 --name test)
