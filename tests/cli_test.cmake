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

function(assert_cli_streams expected_result stdout_pattern stderr_pattern)
    execute_process(
        COMMAND "${INPUT_PROXY}" ${ARGN}
        RESULT_VARIABLE actual_result
        OUTPUT_VARIABLE standard_output
        ERROR_VARIABLE standard_error
    )

    if(expected_result STREQUAL "success")
        if(NOT actual_result EQUAL 0)
            message(FATAL_ERROR "Command unexpectedly failed: ${ARGN}\n${standard_output}${standard_error}")
        endif()
    elseif(actual_result EQUAL 0)
        message(FATAL_ERROR "Command unexpectedly succeeded: ${ARGN}\n${standard_output}${standard_error}")
    endif()

    if(NOT standard_output MATCHES "${stdout_pattern}")
        message(FATAL_ERROR "stdout did not match '${stdout_pattern}': ${ARGN}\n${standard_output}")
    endif()
    if(NOT standard_error MATCHES "${stderr_pattern}")
        message(FATAL_ERROR "stderr did not match '${stderr_pattern}': ${ARGN}\n${standard_error}")
    endif()
endfunction()

function(assert_invalid_instance_name name)
    execute_process(
        COMMAND
            "${INPUT_PROXY}" run
            --source /input-proxy-test-path-that-does-not-exist
            --name "${name}"
        RESULT_VARIABLE actual_result
        OUTPUT_VARIABLE standard_output
        ERROR_VARIABLE standard_error
    )

    string(CONCAT output "${standard_output}" "${standard_error}")
    if(actual_result EQUAL 0)
        message(FATAL_ERROR "Invalid Instance Name succeeded: '${name}'\n${output}")
    endif()
    if(NOT standard_error MATCHES "invalid Instance Name.*1-79 ASCII bytes")
        message(FATAL_ERROR "Invalid Instance Name diagnostic was not actionable: '${name}'\n${output}")
    endif()
    if(output MATCHES "input-proxy: Version=|input-proxy: waiting for source")
        message(FATAL_ERROR "Invalid Instance Name reached runtime startup: '${name}'\n${output}")
    endif()
endfunction()

function(assert_cli_blank_line expected_result)
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

    if(NOT output MATCHES "\n\n$")
        message(FATAL_ERROR "Command output lacks a final blank line: ${ARGN}\n${output}")
    endif()
    if(output MATCHES "\n\n\n$")
        message(FATAL_ERROR "Command output has multiple final blank lines: ${ARGN}\n${output}")
    endif()
endfunction()

function(assert_run_header)
    execute_process(
        COMMAND
            timeout --signal=TERM 1
            "${INPUT_PROXY}" run
            --source /input-proxy-test-path-that-does-not-exist
            --name "Touchscreen_Proxy"
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

    if(NOT standard_output MATCHES "^input-proxy: Version=${INPUT_PROXY_VERSION}")
        message(
            FATAL_ERROR
            "Startup header did not begin with the application version:\n${standard_output}${standard_error}"
        )
    endif()

    set(default_activity
        "input-proxy: activity timeout=5000ms \\(default\\), motion activity while running=yes \\(default\\)\ninput-proxy: detection throttle=250ms \\(default\\), motion activity while paused=yes \\(default\\)"
    )
    if(NOT standard_output MATCHES "${default_activity}")
        message(FATAL_ERROR "Default activity configuration was missing from startup header:\n${standard_output}${standard_error}")
    endif()

    set(expected_context
        "\ninput-proxy: Repository=https://github.com/fasteddy516/input-proxy\ninput-proxy: Source=/input-proxy-test-path-that-does-not-exist\ninput-proxy: InstanceName=Touchscreen_Proxy\ninput-proxy: activity timeout=5000ms (default), motion activity while running=yes (default)\ninput-proxy: detection throttle=250ms (default), motion activity while paused=yes (default)\ninput-proxy: waiting for source"
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

function(assert_run_activity_header expected_running expected_paused)
    execute_process(
        COMMAND
            timeout --signal=TERM 1
            "${INPUT_PROXY}" run
            --source /input-proxy-test-path-that-does-not-exist
            --name "Activity_Header_Test"
            ${ARGN}
        RESULT_VARIABLE actual_result
        OUTPUT_VARIABLE standard_output
        ERROR_VARIABLE standard_error
    )

    if(NOT actual_result EQUAL 124)
        message(FATAL_ERROR "Activity header runtime did not remain active: ${actual_result}\n${standard_output}${standard_error}")
    endif()
    if(NOT standard_output MATCHES "${expected_running}")
        message(FATAL_ERROR "Running activity header did not match '${expected_running}':\n${standard_output}${standard_error}")
    endif()
    if(NOT standard_output MATCHES "${expected_paused}")
        message(FATAL_ERROR "Paused activity header did not match '${expected_paused}':\n${standard_output}${standard_error}")
    endif()
endfunction()

assert_cli(success "input-proxy ${INPUT_PROXY_VERSION}.*Transparent Linux evdev-to-uinput input proxy\\..*Usage:.*Commands:.*Global options:.*Examples:.*https://github.com/fasteddy516/input-proxy" --help)
assert_cli_streams(success "input-proxy ${INPUT_PROXY_VERSION}.*Usage:" "^$" --help)
assert_cli_omits("${INPUT_PROXY}" --help)
assert_cli_omits("input-proxy: Repository=" --help)
assert_cli(success "^input-proxy ${INPUT_PROXY_VERSION}" --version)
assert_cli_streams(success "^input-proxy ${INPUT_PROXY_VERSION}" "^$" --version)
assert_cli_omits("input-proxy: Repository=" --version)
assert_cli(success "Usage:.*input-proxy run --source PATH --name NAME \\[OPTIONS\\].*--source PATH.*--name NAME.*Instance Name.*--activity-timeout-ms MS.*default: 5000.*--detection-throttle-ms MS.*default: 250.*--running-motion-activity on\\|off.*Motion counts as activity while running.*default: on.*--paused-motion-activity on\\|off.*Motion counts as activity while paused.*default: on.*--verbose" run --help)
assert_cli_omits("input-proxy: Repository=" run --help)
assert_cli(success "List available physical input devices concisely\\..*Usage:.*input-proxy list.*Virtual uinput devices are excluded.*currently running input-proxy instances" list --help)
assert_cli_omits("not yet implemented" list --help)
assert_cli(success "Inspect one input device with read-only diagnostics\\..*Usage:.*input-proxy inspect PATH.*Arguments:.*PATH.*without modifying.*Associated runtime instances" inspect --help)
assert_cli_omits("not yet implemented" inspect --help)
assert_cli(failure "missing command.*Usage:")
assert_cli(failure "unknown command 'bogus'.*Usage:" bogus)
assert_cli(failure "invalid run arguments.*Usage:" run)
assert_cli_streams(failure "^$" "invalid run arguments.*Usage:" run)
assert_cli(failure "invalid non-negative duration for --activity-timeout-ms: -1" run --source /missing --name test --activity-timeout-ms -1)
assert_cli(failure "invalid non-negative duration for --activity-timeout-ms: nope" run --source /missing --name test --activity-timeout-ms nope)
assert_cli(failure "invalid non-negative duration for --activity-timeout-ms: 4294967296" run --source /missing --name test --activity-timeout-ms 4294967296)
assert_cli(failure "invalid non-negative duration for --detection-throttle-ms: -1" run --source /missing --name test --detection-throttle-ms -1)
assert_cli(failure "invalid non-negative duration for --detection-throttle-ms: nope" run --source /missing --name test --detection-throttle-ms nope)
assert_cli(failure "invalid value for --running-motion-activity: expected 'on' or 'off'" run --source /missing --name test --running-motion-activity yes)
assert_cli(failure "invalid value for --paused-motion-activity: expected 'on' or 'off'" run --source /missing --name test --paused-motion-activity OFF)
assert_cli_failure_omits("input-proxy: Repository=" run)
assert_invalid_instance_name("")
assert_invalid_instance_name("1touchscreen")
assert_invalid_instance_name("-touchscreen")
assert_invalid_instance_name("touch screen")
assert_invalid_instance_name(" touchscreen")
assert_invalid_instance_name("touchscreen ")
assert_invalid_instance_name("touch.screen")
assert_invalid_instance_name("touch/screen")
assert_invalid_instance_name("touch:screen")
assert_cli(success "DEVICE.*TYPE.*BUS.*NAME" list)
assert_cli_streams(success "DEVICE.*TYPE.*BUS.*NAME" "^$" list)
assert_cli_omits("input-proxy: Repository=" list)
assert_cli(failure "invalid list arguments.*Usage:" list unexpected)
assert_cli(failure "is not an input event device" inspect /dev/input/does-not-exist)
assert_cli_streams(failure "^$" "is not an input event device" inspect /dev/input/does-not-exist)
assert_cli(failure "is not an input event device" inspect /dev/null)
assert_cli(failure "invalid inspect arguments.*Usage:" inspect)
assert_cli(failure "invalid inspect arguments.*Usage:" inspect /dev/input/event0 extra)
assert_cli_failure_omits("input-proxy: Repository=" inspect /dev/input/does-not-exist)
assert_cli(failure "unknown command '--source'.*Usage:" --source /dev/input/event0 --name test)
assert_cli_blank_line(success --help)
assert_cli_blank_line(success --version)
assert_cli_blank_line(success run --help)
assert_cli_blank_line(success list)
assert_cli_blank_line(success list --help)
assert_cli_blank_line(success inspect --help)
assert_cli_blank_line(failure inspect /dev/input/does-not-exist)
assert_cli_blank_line(failure)
assert_cli_blank_line(failure bogus)
assert_cli_blank_line(failure run)
assert_cli_blank_line(failure list unexpected)
assert_cli_blank_line(failure inspect)
assert_run_header()
assert_run_activity_header(
    "activity timeout=5000ms \\(default\\), motion activity while running=no"
    "detection throttle=100ms, motion activity while paused=no"
    --activity-timeout-ms 5000
    --detection-throttle-ms 100
    --running-motion-activity off
    --paused-motion-activity off
)
assert_run_activity_header(
    "activity timeout=5000ms \\(default\\), motion activity while running=yes \\(default\\)"
    "detection throttle=250ms \\(default\\), motion activity while paused=yes \\(default\\)"
    --activity-timeout-ms 5000
    --detection-throttle-ms 250
    --running-motion-activity on
    --paused-motion-activity on
    --verbose
)
assert_run_activity_header(
    "activity timeout=2000ms, motion activity while running=yes \\(default\\)"
    "detection throttle=250ms \\(default\\), motion activity while paused=yes \\(default\\)"
    --activity-timeout-ms 2000
)
