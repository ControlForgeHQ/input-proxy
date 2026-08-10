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

function(assert_cli_failure_omits unexpected_pattern)
    execute_process(
        COMMAND "${INPUT_PROXY}" ${ARGN}
        RESULT_VARIABLE actual_result
        OUTPUT_VARIABLE standard_output
        ERROR_VARIABLE standard_error
    )

    string(CONCAT output "${standard_output}" "${standard_error}")

    if(actual_result EQUAL 0)
        message(FATAL_ERROR "Command unexpectedly succeeded: ${ARGN}\n${output}")
    endif()

    if(output MATCHES "${unexpected_pattern}")
        message(
            FATAL_ERROR
            "Command output unexpectedly matched '${unexpected_pattern}': ${ARGN}\n${output}"
        )
    endif()
endfunction()

function(assert_run_header)
    execute_process(
        COMMAND
            timeout --signal=TERM 1
            "${INPUT_PROXY}" run
            --source /input-proxy-test-path-that-does-not-exist
            --name "Touchscreen Proxy"
        RESULT_VARIABLE actual_result
        OUTPUT_VARIABLE standard_output
        ERROR_VARIABLE standard_error
    )

    if(NOT actual_result EQUAL 124)
        message(
            FATAL_ERROR
            "Runtime did not remain active until stopped: ${actual_result}\n${standard_output}${standard_error}"
        )
    endif()

    if(NOT standard_output MATCHES "^input-proxy: Version=[0-9]+\\.[0-9]+\\.[0-9]+")
        message(
            FATAL_ERROR
            "Startup header did not begin with the application version:\n${standard_output}${standard_error}"
        )
    endif()

    set(expected_context
        "\ninput-proxy: Repository=https://github.com/fasteddy516/input-proxy\ninput-proxy: Source=/input-proxy-test-path-that-does-not-exist\ninput-proxy: DeviceName=Touchscreen Proxy\ninput-proxy: waiting for source"
    )
    string(FIND "${standard_output}" "${expected_context}" context_position)
    if(context_position EQUAL -1)
        message(
            FATAL_ERROR
            "Startup context was missing or did not precede lifecycle output:\n${standard_output}${standard_error}"
        )
    endif()

    string(REGEX MATCHALL "input-proxy: Repository=https://github.com/fasteddy516/input-proxy" header_matches "${standard_output}")
    list(LENGTH header_matches header_count)
    if(NOT header_count EQUAL 1)
        message(FATAL_ERROR "Expected exactly one startup header:\n${standard_output}")
    endif()
endfunction()

assert_cli(success "input-proxy [0-9]+\\.[0-9]+\\.[0-9]+.*Transparent Linux evdev-to-uinput input proxy\\..*Usage:.*Commands:.*Global options:.*Examples:.*https://github.com/fasteddy516/input-proxy" --help)
assert_cli_omits("${INPUT_PROXY}" --help)
assert_cli_omits("input-proxy: Repository=" --help)
assert_cli(success "input-proxy [0-9]+\\.[0-9]+\\.[0-9]+" --version)
assert_cli_omits("input-proxy: Repository=" --version)
assert_cli(success "Usage:.*input-proxy run --source PATH --name NAME \\[--verbose\\].*--source PATH.*--name NAME.*--verbose" run --help)
assert_cli_omits("input-proxy: Repository=" run --help)
assert_cli(success "List available physical input devices concisely\\..*Usage:.*input-proxy list.*Virtual uinput devices are excluded" list --help)
assert_cli_omits("not yet implemented" list --help)
assert_cli(success "Inspect one input device with read-only diagnostics\\..*Usage:.*input-proxy inspect PATH.*Arguments:.*PATH.*without modifying" inspect --help)
assert_cli_omits("not yet implemented" inspect --help)
assert_cli(failure "missing command.*Usage:")
assert_cli(failure "unknown command 'bogus'.*Usage:" bogus)
assert_cli(failure "invalid run arguments.*Usage:" run)
assert_cli_failure_omits("input-proxy: Repository=" run)
assert_cli(success "DEVICE.*TYPE.*BUS:VENDOR:PRODUCT.*NAME" list)
assert_cli_omits("input-proxy: Repository=" list)
assert_cli(failure "invalid list arguments.*Usage:" list unexpected)
assert_cli(failure "command 'inspect' is not yet implemented" inspect /dev/input/event0)
assert_cli_failure_omits("input-proxy: Repository=" inspect /dev/input/event0)
assert_cli(failure "unknown command '--source'.*Usage:" --source /dev/input/event0 --name test)
assert_run_header()
