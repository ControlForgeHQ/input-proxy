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

function(assert_cli_omits unexpected_pattern)
    execute_process(
        COMMAND "${INPUT_PROXY}" ${ARGN}
        RESULT_VARIABLE actual_result
        OUTPUT_VARIABLE standard_output
        ERROR_VARIABLE standard_error
    )

    string(CONCAT output "${standard_output}" "${standard_error}")

    if(NOT actual_result EQUAL 0)
        message(FATAL_ERROR "Command unexpectedly failed: ${ARGN}\n${output}")
    endif()

    if(output MATCHES "${unexpected_pattern}")
        message(
            FATAL_ERROR
            "Command output unexpectedly matched '${unexpected_pattern}': ${ARGN}\n${output}"
        )
    endif()
endfunction()

assert_cli(success "input-proxy [0-9]+\\.[0-9]+\\.[0-9]+.*Transparent Linux evdev-to-uinput input proxy\\..*Usage:.*Commands:.*Global options:.*Examples:.*https://github.com/fasteddy516/input-proxy" --help)
assert_cli_omits("${INPUT_PROXY}" --help)
assert_cli(success "input-proxy [0-9]+\\.[0-9]+\\.[0-9]+" --version)
assert_cli(success "Usage:.*input-proxy run --source PATH --name NAME \\[--verbose\\].*--source PATH.*--name NAME.*--verbose" run --help)
assert_cli(success "List available physical input devices concisely\\..*Usage:.*input-proxy list.*Virtual uinput devices are excluded" list --help)
assert_cli_omits("not yet implemented" list --help)
assert_cli(success "Inspect one input device with read-only diagnostics\\..*Usage:.*input-proxy inspect PATH.*Arguments:.*PATH.*without modifying" inspect --help)
assert_cli_omits("not yet implemented" inspect --help)
assert_cli(failure "missing command.*Usage:")
assert_cli(failure "unknown command 'bogus'.*Usage:" bogus)
assert_cli(failure "invalid run arguments.*Usage:" run)
assert_cli(failure "command 'list' is not yet implemented" list)
assert_cli(failure "command 'inspect' is not yet implemented" inspect /dev/input/event0)
assert_cli(failure "unknown command '--source'.*Usage:" --source /dev/input/event0 --name test)
