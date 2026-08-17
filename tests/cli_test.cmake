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
        "input-proxy: activity timeout=5000ms \\(default\\), motion activity while running=on \\(default\\)\ninput-proxy: detection throttle=250ms \\(default\\), motion activity while paused=on \\(default\\)\ninput-proxy: start paused=off \\(default\\)"
    )
    if(NOT standard_output MATCHES "${default_activity}")
        message(FATAL_ERROR "Default activity configuration was missing from startup header:\n${standard_output}${standard_error}")
    endif()

    set(expected_context
        "\ninput-proxy: Repository=https://github.com/ControlForgeHQ/input-proxy\ninput-proxy: Source=/input-proxy-test-path-that-does-not-exist\ninput-proxy: InstanceName=Touchscreen_Proxy\ninput-proxy: activity timeout=5000ms (default), motion activity while running=on (default)\ninput-proxy: detection throttle=250ms (default), motion activity while paused=on (default)\ninput-proxy: start paused=off (default)\ninput-proxy: waiting for source"
    )
    string(FIND "${standard_output}" "${expected_context}" context_position)
    if(context_position EQUAL -1)
        message(
            FATAL_ERROR
            "Startup context was missing or did not precede lifecycle output:\n${standard_output}${standard_error}"
        )
    endif()

    string(REGEX MATCHALL "input-proxy: Repository=https://github.com/ControlForgeHQ/input-proxy" header_matches "${standard_output}")
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

function(assert_run_start_paused_header value expected_pattern)
    execute_process(
        COMMAND
            timeout --signal=TERM 1
            "${INPUT_PROXY}" run
            --source /input-proxy-test-path-that-does-not-exist
            --name "Start_Paused_Header_Test"
            --start-paused "${value}"
        RESULT_VARIABLE actual_result
        OUTPUT_VARIABLE standard_output
        ERROR_VARIABLE standard_error
    )

    if(NOT actual_result EQUAL 124)
        message(FATAL_ERROR "Start-paused runtime did not remain active: ${actual_result}\n${standard_output}${standard_error}")
    endif()
    if(NOT standard_output MATCHES "${expected_pattern}")
        message(FATAL_ERROR "Start-paused header did not match '${expected_pattern}':\n${standard_output}${standard_error}")
    endif()
endfunction()

function(write_response_file path)
    string(CONCAT content ${ARGN})
    file(WRITE "${path}" "${content}")
endfunction()

function(assert_response_failure path expected_pattern)
    assert_cli_streams(failure "^$" "${expected_pattern}" run "@${path}")
    assert_cli_failure_omits("input-proxy: Version=" run "@${path}")
    assert_cli_failure_omits("input-proxy: waiting for source" run "@${path}")
endfunction()

set(response_dir "${CMAKE_CURRENT_BINARY_DIR}/cli-response-files")
file(MAKE_DIRECTORY "${response_dir}")

set(complete_response "${response_dir}/complete.args")
write_response_file("${complete_response}"
    "--source\n/input proxy path with spaces\n--name\nResponse_File_Test\n"
    "--activity-timeout-ms\n2000\n--detection-throttle-ms\n100\n"
    "--running-motion-activity\noff\n--paused-motion-activity\noff\n"
    "--start-paused\non")

set(invalid_value_response "${response_dir}/invalid-value.args")
write_response_file("${invalid_value_response}"
    "--source\n/missing\n--name\ntest\n--start-paused\nyes\n")

set(missing_option_response "${response_dir}/missing-option.args")
write_response_file("${missing_option_response}" "--source\n/missing\n")

set(duplicate_option_response "${response_dir}/duplicate-option.args")
write_response_file("${duplicate_option_response}"
    "--source\n/missing\n--source\n/other\n--name\ntest\n")

set(unknown_option_response "${response_dir}/unknown-option.args")
write_response_file("${unknown_option_response}"
    "--source\n/missing\n--name\ntest\n--unknown\n")

set(blank_line_response "${response_dir}/blank-line.args")
write_response_file("${blank_line_response}"
    "--source\n/missing\n\n--name\ntest\n")

set(literal_response "${response_dir}/literal.args")
write_response_file("${literal_response}"
    "--source\n@nested.args\n--name\nLiteral_Test\n")

set(special_character_response "${response_dir}/special-characters.args")
write_response_file("${special_character_response}"
    "--source\n\"$HOME\\literal path~\"\n--name\nSpecial_Character_Test\n")

set(nul_response "${response_dir}/nul.args")
execute_process(
    COMMAND /bin/sh -c "printf '\\055\\055source\\012/missing\\000suffix\\012\\055\\055name\\012test\\012' > \"$1\"" sh "${nul_response}"
    RESULT_VARIABLE nul_fixture_result
)
if(NOT nul_fixture_result EQUAL 0)
    message(FATAL_ERROR "Failed to create embedded-NUL response fixture")
endif()

assert_cli(success "input-proxy ${INPUT_PROXY_VERSION}.*Transparent Linux evdev-to-uinput input proxy\\..*Usage:.*Commands:.*Global options:.*Examples:.*https://github.com/ControlForgeHQ/input-proxy" --help)
assert_cli_streams(success "input-proxy ${INPUT_PROXY_VERSION}.*Usage:" "^$" --help)
assert_cli_omits("${INPUT_PROXY}" --help)
assert_cli_omits("input-proxy: Repository=" --help)
assert_cli(success "^input-proxy ${INPUT_PROXY_VERSION}" --version)
assert_cli_streams(success "^input-proxy ${INPUT_PROXY_VERSION}" "^$" --version)
assert_cli_omits("input-proxy: Repository=" --version)
assert_cli(success "Usage:.*input-proxy run --source PATH --name NAME \\[OPTIONS\\].*input-proxy run @file.*one runtime argument per line.*--source PATH.*--name NAME.*Instance Name.*--activity-timeout-ms MS.*default: 5000.*--detection-throttle-ms MS.*default: 250.*--running-motion-activity on\\|off.*Motion counts as activity while running.*default: on.*--paused-motion-activity on\\|off.*Motion counts as activity while paused.*default: on.*--start-paused on\\|off.*Initial session pause policy.*default: off.*--verbose" run --help)
assert_cli_omits("input-proxy: Repository=" run --help)
assert_cli(success "List available physical input devices concisely\\..*Usage:.*input-proxy list.*Virtual uinput devices are excluded.*currently running input-proxy instances" list --help)
assert_cli_omits("not yet implemented" list --help)
assert_cli(success "Inspect one input device with read-only diagnostics\\..*Usage:.*input-proxy inspect PATH.*Arguments:.*PATH.*without modifying.*Associated runtime instances" inspect --help)
assert_cli(success "Usage:.*input-proxy install \\[OPTIONS\\].*--source PATH.*--name INSTANCE_NAME.*--activity-timeout-ms MS.*--detection-throttle-ms MS.*--running-motion-activity on\\|off.*--paused-motion-activity on\\|off.*--start-paused on\\|off.*--use-preferred-run-source yes\\|no.*--add-source-permission-rule yes\\|no.*--add-libinput-ignore-rule yes\\|no" install --help)
assert_cli_streams(success "Plan installation" "^$" install --help)
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
assert_cli(failure "invalid value for --start-paused: expected 'on' or 'off'" run --source /missing --name test --start-paused yes)
assert_cli_failure_omits("input-proxy: Repository=" run --source /missing --name test --start-paused yes)
assert_cli_failure_omits("input-proxy: Repository=" run)
assert_cli(failure "response-file path after '@' is empty" run "@")
assert_response_failure("${response_dir}/does-not-exist.args" "cannot open response file.*does-not-exist.args")
assert_response_failure("${invalid_value_response}" "invalid value for --start-paused: expected 'on' or 'off'")
assert_response_failure("${missing_option_response}" "invalid run arguments")
assert_response_failure("${duplicate_option_response}" "invalid run arguments")
assert_response_failure("${unknown_option_response}" "invalid run arguments")
assert_response_failure("${blank_line_response}" "invalid run arguments")
assert_response_failure("${nul_response}" "contains an embedded NUL byte")
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
assert_run_start_paused_header(on "start paused=on")
assert_run_start_paused_header(off "start paused=off \\(default\\)")
assert_run_activity_header(
    "activity timeout=5000ms \\(default\\), motion activity while running=off"
    "detection throttle=100ms, motion activity while paused=off"
    --activity-timeout-ms 5000
    --detection-throttle-ms 100
    --running-motion-activity off
    --paused-motion-activity off
)
assert_run_activity_header(
    "activity timeout=5000ms \\(default\\), motion activity while running=on \\(default\\)"
    "detection throttle=250ms \\(default\\), motion activity while paused=on \\(default\\)"
    --activity-timeout-ms 5000
    --detection-throttle-ms 250
    --running-motion-activity on
    --paused-motion-activity on
    --verbose
)
assert_run_activity_header(
    "activity timeout=2000ms, motion activity while running=on \\(default\\)"
    "detection throttle=250ms \\(default\\), motion activity while paused=on \\(default\\)"
    --activity-timeout-ms 2000
)
execute_process(
    COMMAND timeout --signal=TERM 1 "${INPUT_PROXY}" run "@${complete_response}"
    RESULT_VARIABLE response_result
    OUTPUT_VARIABLE response_output
    ERROR_VARIABLE response_error
)
if(NOT response_result EQUAL 124)
    message(FATAL_ERROR "Complete response-file runtime did not remain active: ${response_result}\n${response_output}${response_error}")
endif()
set(response_header
    "input-proxy: Source=/input proxy path with spaces\ninput-proxy: InstanceName=Response_File_Test\ninput-proxy: activity timeout=2000ms, motion activity while running=off\ninput-proxy: detection throttle=100ms, motion activity while paused=off\ninput-proxy: start paused=on"
)
if(NOT response_output MATCHES "${response_header}")
    message(FATAL_ERROR "Response-file configuration was not parsed through the runtime path:\n${response_output}${response_error}")
endif()

foreach(literal_path IN ITEMS "${literal_response}" "${special_character_response}")
    execute_process(
        COMMAND timeout --signal=TERM 1 "${INPUT_PROXY}" run "@${literal_path}"
        RESULT_VARIABLE literal_result
        OUTPUT_VARIABLE literal_output
        ERROR_VARIABLE literal_error
    )
    if(NOT literal_result EQUAL 124)
        message(FATAL_ERROR "Literal response-file runtime did not remain active: ${literal_result}\n${literal_output}${literal_error}")
    endif()
    if(literal_path STREQUAL "${literal_response}")
        set(expected_source "input-proxy: Source=@nested.args")
    else()
        set(expected_source "input-proxy: Source=\"$HOME\\literal path~\"")
    endif()
    string(FIND "${literal_output}" "${expected_source}" source_position)
    if(source_position EQUAL -1)
        message(FATAL_ERROR "Response-file literal characters were not preserved:\n${literal_output}${literal_error}")
    endif()
endforeach()
