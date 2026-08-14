#define _POSIX_C_SOURCE 200809L

#include <input_proxy/proxy_session.h>
#include <input_proxy/source_device.h>
#include <input_proxy/virtual_device.h>

#include "proxy_session_internal.h"
#include "runtime_control_internal.h"
#include "source_device_internal.h"

#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static enum input_proxy_result read_result;
static enum input_proxy_result write_result;
static enum input_proxy_result open_result;
static enum input_proxy_result create_result;
static enum input_proxy_result neutralize_result;
static enum input_proxy_result capture_state_result;
static enum input_proxy_result state_write_result;
static enum input_proxy_result source_available_result;
static int fail_create_call;
static bool compatibility_results[8];
static enum input_proxy_result open_results[32];
static enum input_proxy_result read_results[8];
static enum input_proxy_result sync_read_results[8];
static struct input_event sync_events[8];
static struct input_event source_event;
static struct input_event written_events[8];
static struct input_event state_written_events[16];
static struct input_event captured_state_events[8];
static size_t captured_state_event_count;
static char operations[64];
static int operation_count;
static int open_calls;
static int open_result_count;
static int create_calls;
static int compatibility_calls;
static int close_calls;
static int destroy_calls;
static int neutralize_calls;
static int capture_state_calls;
static int source_available_calls;
static int read_calls;
static int write_calls;
static int state_write_calls;
static size_t state_writes_remaining;
static int fail_state_write_call;
static char barrier_operations[64];
static int barrier_operation_count;
/* T/F are availability transitions; N is neutralization; C is source close. */
static char availability_operations[64];
static int availability_operation_count;
static int read_result_count;
static int sync_read_calls;
static int sync_read_result_count;
static int event_sleep_calls;
static int source_sleep_calls;
static int sleep_duration_failures;
static struct input_proxy_session *running_session;
static bool shutdown_after_write;
static bool shutdown_during_event_sleep;
static bool shutdown_during_source_sleep;
static int shutdown_after_source_sleep_calls;
static long long monotonic_time_ns;

static struct input_proxy_source_device *const test_source_device =
    (struct input_proxy_source_device *)1;
static struct input_proxy_virtual_device *const test_virtual_device =
    (struct input_proxy_virtual_device *)1;

size_t __real_input_proxy_runtime_control_apply_changes(
    struct input_proxy_runtime_control **control,
    struct input_proxy_runtime_control_state *state,
    const struct input_proxy_runtime_control_changes *changes);

size_t __wrap_input_proxy_runtime_control_apply_changes(
    struct input_proxy_runtime_control **control,
    struct input_proxy_runtime_control_state *state,
    const struct input_proxy_runtime_control_changes *changes)
{
    const size_t changed_count =
        __real_input_proxy_runtime_control_apply_changes(
            control, state, changes);

    if (changed_count > 0 &&
        (changes->properties &
         INPUT_PROXY_RUNTIME_CONTROL_SOURCE_AVAILABLE) != 0U) {
        availability_operations[availability_operation_count++] =
            state->source_available ? 'T' : 'F';
    }

    return changed_count;
}

int clock_gettime(clockid_t clock_id, struct timespec *time)
{
    if (clock_id != CLOCK_MONOTONIC) {
        return -1;
    }

    time->tv_sec = monotonic_time_ns / 1000000000LL;
    time->tv_nsec = monotonic_time_ns % 1000000000LL;
    return 0;
}

int nanosleep(const struct timespec *duration, struct timespec *remaining)
{
    (void)remaining;
    if (duration->tv_sec == 0 && duration->tv_nsec == 10000000L) {
        event_sleep_calls++;
        if (shutdown_during_event_sleep) {
            input_proxy_session_request_shutdown(running_session);
        }
    } else if (duration->tv_sec == 0 && duration->tv_nsec == 100000000L) {
        source_sleep_calls++;
        monotonic_time_ns += duration->tv_nsec;
        if (shutdown_during_source_sleep ||
            (shutdown_after_source_sleep_calls > 0 &&
             source_sleep_calls >= shutdown_after_source_sleep_calls)) {
            input_proxy_session_request_shutdown(running_session);
        }
    } else {
        sleep_duration_failures++;
    }
    return 0;
}

enum input_proxy_result input_proxy_source_device_open(
    struct input_proxy_source_device **device,
    const char *source_path)
{
    enum input_proxy_result result = open_result;

    if (open_calls < open_result_count) {
        result = open_results[open_calls];
    }
    open_calls++;
    if (strcmp(source_path, "/dev/input/event-test") != 0) {
        return INPUT_PROXY_ERROR_INTERNAL;
    }
    if (result == INPUT_PROXY_SUCCESS) {
        *device = test_source_device;
    }
    return result;
}

void input_proxy_source_device_close(struct input_proxy_source_device *device)
{
    if (device != NULL) {
        close_calls++;
        operations[operation_count++] = 'C';
        availability_operations[availability_operation_count++] = 'C';
    }
}

enum input_proxy_result input_proxy_source_device_capture_state(
    const struct input_proxy_source_device *device,
    struct input_proxy_source_state *state)
{
    if (device != test_source_device || state == NULL) {
        return INPUT_PROXY_ERROR_INTERNAL;
    }

    capture_state_calls++;
    barrier_operations[barrier_operation_count++] = 'Q';
    if (capture_state_result == INPUT_PROXY_SUCCESS) {
        state->events = captured_state_events;
        state->event_count = captured_state_event_count;
        state_writes_remaining = captured_state_event_count + 1;
    }
    return capture_state_result;
}

void input_proxy_source_state_destroy(struct input_proxy_source_state *state)
{
    if (state != NULL) {
        state->events = NULL;
        state->event_count = 0;
    }
}

enum input_proxy_result input_proxy_source_device_check_available(
    const struct input_proxy_source_device *device)
{
    if (device != test_source_device) {
        return INPUT_PROXY_ERROR_INTERNAL;
    }

    source_available_calls++;
    barrier_operations[barrier_operation_count++] = 'A';
    return source_available_result;
}

enum input_proxy_result input_proxy_virtual_device_create(
    struct input_proxy_virtual_device **device,
    const struct input_proxy_source_device *source_device,
    const char *device_name)
{
    create_calls++;
    if (source_device != test_source_device ||
        strcmp(device_name, "proxy_test_device") != 0) {
        return INPUT_PROXY_ERROR_INTERNAL;
    }
    if (create_calls == fail_create_call) {
        return INPUT_PROXY_ERROR_VIRTUAL_DEVICE_CREATE_FAILED;
    }
    if (create_result == INPUT_PROXY_SUCCESS) {
        *device = test_virtual_device;
    }
    return create_result;
}

bool input_proxy_virtual_device_is_compatible(
    const struct input_proxy_virtual_device *device,
    const struct input_proxy_source_device *source_device)
{
    if (device != test_virtual_device || source_device != test_source_device) {
        return false;
    }

    return compatibility_results[compatibility_calls++];
}

enum input_proxy_result input_proxy_virtual_device_neutralize(
    struct input_proxy_virtual_device *device)
{
    if (device != test_virtual_device) {
        return INPUT_PROXY_ERROR_INTERNAL;
    }

    neutralize_calls++;
    operations[operation_count++] = 'N';
    availability_operations[availability_operation_count++] = 'N';
    return neutralize_result;
}

void input_proxy_virtual_device_destroy(
    struct input_proxy_virtual_device *device)
{
    if (device != NULL) {
        destroy_calls++;
        operations[operation_count++] = 'D';
    }
}

enum input_proxy_result input_proxy_source_device_read_event(
    struct input_proxy_source_device *device,
    struct input_event *event)
{
    (void)device;
    if (read_result_count > 0) {
        read_result = read_results[read_calls];
    }
    read_calls++;
    barrier_operations[barrier_operation_count++] = 'R';

    if (read_result == INPUT_PROXY_SUCCESS) {
        *event = source_event;
    }

    return read_result;
}

enum input_proxy_result input_proxy_source_device_read_sync_event(
    struct input_proxy_source_device *device,
    struct input_event *event)
{
    enum input_proxy_result result;

    (void)device;
    result = sync_read_results[sync_read_calls];
    if (sync_read_calls < sync_read_result_count) {
        sync_read_calls++;
    }
    if (result == INPUT_PROXY_SUCCESS) {
        *event = sync_events[sync_read_calls - 1];
    }

    return result;
}

enum input_proxy_result input_proxy_virtual_device_write_event(
    struct input_proxy_virtual_device *device,
    const struct input_event *event)
{
    (void)device;

    if (state_writes_remaining > 0) {
        if (state_write_calls < 16) {
            state_written_events[state_write_calls] = *event;
        }
        state_write_calls++;
        state_writes_remaining--;
        barrier_operations[barrier_operation_count++] =
            event->type == EV_SYN && event->code == SYN_REPORT ? 'B' : 'T';
        if (state_write_calls == fail_state_write_call) {
            return INPUT_PROXY_ERROR_EVENT_WRITE_FAILED;
        }
        return state_write_result;
    }

    if (write_calls < 8) {
        written_events[write_calls] = *event;
    }
    write_calls++;
    barrier_operations[barrier_operation_count++] = 'F';

    if (shutdown_after_write) {
        input_proxy_session_request_shutdown(running_session);
    }

    return write_result;
}

static int expect_result(
    const char *test_name,
    enum input_proxy_result actual,
    enum input_proxy_result expected)
{
    if (actual == expected) {
        return 0;
    }

    fprintf(
        stderr,
        "%s: expected %s, got %s\n",
        test_name,
        input_proxy_result_string(expected),
        input_proxy_result_string(actual)
    );
    return 1;
}

static int expect_no_write(const char *test_name, int previous_write_calls)
{
    if (write_calls == previous_write_calls) {
        return 0;
    }

    fprintf(stderr, "%s: event was unexpectedly written\n", test_name);
    return 1;
}

static void reset_runtime(void)
{
    open_result = INPUT_PROXY_SUCCESS;
    create_result = INPUT_PROXY_SUCCESS;
    neutralize_result = INPUT_PROXY_SUCCESS;
    capture_state_result = INPUT_PROXY_SUCCESS;
    state_write_result = INPUT_PROXY_SUCCESS;
    source_available_result = INPUT_PROXY_SUCCESS;
    fail_create_call = 0;
    read_result = INPUT_PROXY_SUCCESS;
    write_result = INPUT_PROXY_SUCCESS;
    operation_count = 0;
    open_calls = 0;
    open_result_count = 0;
    create_calls = 0;
    compatibility_calls = 0;
    close_calls = 0;
    destroy_calls = 0;
    neutralize_calls = 0;
    capture_state_calls = 0;
    source_available_calls = 0;
    captured_state_event_count = 0;
    read_calls = 0;
    write_calls = 0;
    state_write_calls = 0;
    state_writes_remaining = 0;
    fail_state_write_call = 0;
    barrier_operation_count = 0;
    availability_operation_count = 0;
    read_result_count = 0;
    sync_read_calls = 0;
    sync_read_result_count = 0;
    event_sleep_calls = 0;
    source_sleep_calls = 0;
    sleep_duration_failures = 0;
    running_session = NULL;
    shutdown_after_write = false;
    shutdown_during_event_sleep = false;
    shutdown_during_source_sleep = false;
    shutdown_after_source_sleep_calls = 0;
    monotonic_time_ns = 0;
    memset(operations, 0, sizeof(operations));
    memset(state_written_events, 0, sizeof(state_written_events));
    memset(barrier_operations, 0, sizeof(barrier_operations));
    memset(availability_operations, 0, sizeof(availability_operations));
    memset(compatibility_results, 0, sizeof(compatibility_results));
}

static int run_runtime_test(
    const char *test_name,
    const struct input_proxy_session_config *config,
    enum input_proxy_result expected)
{
    struct input_proxy_session *session = NULL;
    int failures = 0;

    failures += expect_result(
        test_name,
        input_proxy_session_create(&session, config),
        INPUT_PROXY_SUCCESS
    );
    if (session == NULL) {
        return failures + 1;
    }

    running_session = session;
    failures += expect_result(
        test_name,
        input_proxy_session_run(session),
        expected
    );
    running_session = NULL;
    input_proxy_session_destroy(session);
    return failures;
}

static int run_runtime_test_with_output(
    const char *test_name,
    const struct input_proxy_session_config *config,
    enum input_proxy_result expected,
    char *output,
    size_t output_size)
{
    FILE *capture;
    int saved_stdout;
    int failures;
    size_t bytes_read;

    capture = tmpfile();
    if (capture == NULL) {
        fprintf(stderr, "%s: failed to create output capture\n", test_name);
        return 1;
    }

    fflush(stdout);
    saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdout < 0 || dup2(fileno(capture), STDOUT_FILENO) < 0) {
        fprintf(stderr, "%s: failed to redirect stdout\n", test_name);
        if (saved_stdout >= 0) {
            close(saved_stdout);
        }
        fclose(capture);
        return 1;
    }

    failures = run_runtime_test(test_name, config, expected);
    fflush(stdout);
    (void)dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);

    rewind(capture);
    bytes_read = fread(output, 1, output_size - 1, capture);
    output[bytes_read] = '\0';
    fclose(capture);

    return failures;
}

static int count_occurrences(const char *text, const char *substring)
{
    int count = 0;
    size_t length = strlen(substring);

    while ((text = strstr(text, substring)) != NULL) {
        count++;
        text += length;
    }

    return count;
}

int main(void)
{
    const struct input_proxy_session_config config = {
        .source_path = "/dev/input/event-test",
        .instance_name = "proxy_test_device",
        .verbose = false
    };
    const struct input_proxy_session_config verbose_config = {
        .source_path = "/dev/input/event-test",
        .instance_name = "proxy_test_device",
        .verbose = true
    };
    struct input_proxy_source_device *const source_device =
        (struct input_proxy_source_device *)1;
    struct input_proxy_virtual_device *const virtual_device =
        (struct input_proxy_virtual_device *)1;
    struct input_proxy_session *session;
    char output[4096];
    int previous_write_calls;
    int failures = 0;

    {
        const struct input_proxy_session_config invalid_config = {
            .source_path = "/dev/input/event-test",
            .instance_name = "invalid name",
            .verbose = false
        };

        reset_runtime();
        session = (struct input_proxy_session *)1;
        failures += expect_result(
            "invalid Instance Name",
            input_proxy_session_create(&session, &invalid_config),
            INPUT_PROXY_ERROR_INVALID_INSTANCE_NAME
        );
        if (session != NULL || open_calls != 0 || create_calls != 0) {
            fprintf(
                stderr,
                "invalid Instance Name: runtime activity occurred\n"
            );
            failures++;
        }
    }

    reset_runtime();

    failures += expect_result(
        "session creation",
        input_proxy_session_create(&session, &config),
        INPUT_PROXY_SUCCESS
    );
    if (session == NULL) {
        return 1;
    }

    captured_state_events[0] = (struct input_event) {
        .type = EV_KEY, .code = KEY_A, .value = 1
    };
    captured_state_events[1] = (struct input_event) {
        .type = EV_SW, .code = SW_LID, .value = 1
    };
    captured_state_events[2] = (struct input_event) {
        .type = EV_ABS, .code = ABS_X, .value = 321
    };
    captured_state_event_count = 3;
    failures += expect_result(
        "current-state synchronization",
        input_proxy_session_synchronize_state(
            session,
            source_device,
            virtual_device
        ),
        INPUT_PROXY_SUCCESS
    );
    if (capture_state_calls != 1 || source_available_calls != 1 ||
        state_write_calls != 4 ||
        memcmp(&state_written_events[0], &captured_state_events[0],
               sizeof(state_written_events[0])) != 0 ||
        memcmp(&state_written_events[1], &captured_state_events[1],
               sizeof(state_written_events[1])) != 0 ||
        memcmp(&state_written_events[2], &captured_state_events[2],
               sizeof(state_written_events[2])) != 0 ||
        state_written_events[3].type != EV_SYN ||
        state_written_events[3].code != SYN_REPORT) {
        fprintf(stderr, "current-state synchronization: bad event sequence\n");
        failures++;
    }

    capture_state_result = INPUT_PROXY_ERROR_EVENT_READ_FAILED;
    previous_write_calls = state_write_calls;
    failures += expect_result(
        "current-state query failure",
        input_proxy_session_synchronize_state(
            session,
            source_device,
            virtual_device
        ),
        INPUT_PROXY_ERROR_EVENT_READ_FAILED
    );
    if (state_write_calls != previous_write_calls) {
        fprintf(stderr, "current-state query failure: unexpected write\n");
        failures++;
    }

    capture_state_result = INPUT_PROXY_SUCCESS;
    state_write_result = INPUT_PROXY_ERROR_EVENT_WRITE_FAILED;
    failures += expect_result(
        "current-state write failure",
        input_proxy_session_synchronize_state(
            session,
            source_device,
            virtual_device
        ),
        INPUT_PROXY_ERROR_EVENT_WRITE_FAILED
    );
    state_write_result = INPUT_PROXY_SUCCESS;
    captured_state_event_count = 0;
    write_calls = 0;
    state_write_calls = 0;
    state_writes_remaining = 0;
    barrier_operation_count = 0;
    capture_state_calls = 0;
    source_available_calls = 0;
    operation_count = 0;
    memset(written_events, 0, sizeof(written_events));
    memset(operations, 0, sizeof(operations));
    memset(barrier_operations, 0, sizeof(barrier_operations));

    failures += expect_result(
        "null session",
        input_proxy_session_process_event(
            NULL,
            source_device,
            virtual_device
        ),
        INPUT_PROXY_ERROR_INVALID_ARGUMENT
    );
    failures += expect_result(
        "null source",
        input_proxy_session_process_event(session, NULL, virtual_device),
        INPUT_PROXY_ERROR_INVALID_ARGUMENT
    );
    failures += expect_result(
        "null virtual device",
        input_proxy_session_process_event(session, source_device, NULL),
        INPUT_PROXY_ERROR_INVALID_ARGUMENT
    );

    read_result = INPUT_PROXY_SUCCESS;
    write_result = INPUT_PROXY_SUCCESS;
    source_event = (struct input_event) {
        .time = { .tv_sec = 123, .tv_usec = 456 },
        .type = EV_KEY,
        .code = KEY_A,
        .value = 1
    };
    failures += expect_result(
        "successful forwarding",
        input_proxy_session_process_event(
            session,
            source_device,
            virtual_device
        ),
        INPUT_PROXY_SUCCESS
    );

    source_event = (struct input_event) {
        .time = { .tv_sec = 789, .tv_usec = 12 },
        .type = EV_SYN,
        .code = SYN_REPORT,
        .value = 0
    };
    failures += expect_result(
        "synchronization boundary forwarding",
        input_proxy_session_process_event(
            session,
            source_device,
            virtual_device
        ),
        INPUT_PROXY_SUCCESS
    );
    if (write_calls != 2 ||
        written_events[0].type != EV_KEY ||
        written_events[0].code != KEY_A ||
        written_events[0].value != 1 ||
        memcmp(&written_events[1], &source_event, sizeof(source_event)) != 0) {
        fprintf(stderr, "forwarded events were changed or reordered\n");
        failures++;
    }

    read_result = INPUT_PROXY_EVENT_UNAVAILABLE;
    previous_write_calls = write_calls;
    failures += expect_result(
        "event unavailable",
        input_proxy_session_process_event(
            session,
            source_device,
            virtual_device
        ),
        INPUT_PROXY_EVENT_UNAVAILABLE
    );
    failures += expect_no_write("event unavailable", previous_write_calls);

    read_result = INPUT_PROXY_EVENT_SYNC_REQUIRED;
    sync_read_results[0] = INPUT_PROXY_EVENT_UNAVAILABLE;
    sync_read_result_count = 1;
    previous_write_calls = write_calls;
    failures += expect_result(
        "synchronization required",
        input_proxy_session_process_event(
            session,
            source_device,
            virtual_device
        ),
        INPUT_PROXY_SUCCESS
    );
    failures += expect_no_write("synchronization required", previous_write_calls);

    sync_read_results[0] = INPUT_PROXY_SUCCESS;
    sync_read_results[1] = INPUT_PROXY_SUCCESS;
    sync_read_results[2] = INPUT_PROXY_EVENT_UNAVAILABLE;
    sync_events[0] = (struct input_event) {
        .type = EV_KEY, .code = KEY_C, .value = 1
    };
    sync_events[1] = (struct input_event) {
        .type = EV_SYN, .code = SYN_REPORT, .value = 0
    };
    sync_read_calls = 0;
    sync_read_result_count = 3;
    failures += expect_result(
        "multiple synchronization events",
        input_proxy_session_process_event(session, source_device, virtual_device),
        INPUT_PROXY_SUCCESS
    );
    if (write_calls != previous_write_calls + 2 ||
        memcmp(&written_events[previous_write_calls], &sync_events[0],
               sizeof(sync_events[0])) != 0 ||
        memcmp(&written_events[previous_write_calls + 1], &sync_events[1],
               sizeof(sync_events[1])) != 0) {
        fprintf(stderr, "synchronization events were changed or reordered\n");
        failures++;
    }

    sync_read_results[0] = INPUT_PROXY_ERROR_SOURCE_DISCONNECTED;
    sync_read_calls = 0;
    sync_read_result_count = 1;
    failures += expect_result(
        "sync source disconnected",
        input_proxy_session_process_event(session, source_device, virtual_device),
        INPUT_PROXY_ERROR_SOURCE_DISCONNECTED
    );

    sync_read_results[0] = INPUT_PROXY_ERROR_EVENT_READ_FAILED;
    sync_read_calls = 0;
    failures += expect_result(
        "sync read failure",
        input_proxy_session_process_event(session, source_device, virtual_device),
        INPUT_PROXY_ERROR_EVENT_READ_FAILED
    );

    sync_read_results[0] = INPUT_PROXY_SUCCESS;
    sync_events[0] = (struct input_event) {
        .type = EV_KEY, .code = KEY_D, .value = 0
    };
    sync_read_calls = 0;
    write_result = INPUT_PROXY_ERROR_EVENT_WRITE_FAILED;
    failures += expect_result(
        "sync write failure",
        input_proxy_session_process_event(session, source_device, virtual_device),
        INPUT_PROXY_ERROR_EVENT_WRITE_FAILED
    );
    write_result = INPUT_PROXY_SUCCESS;

    read_result = INPUT_PROXY_ERROR_SOURCE_DISCONNECTED;
    previous_write_calls = write_calls;
    failures += expect_result(
        "source disconnected",
        input_proxy_session_process_event(
            session,
            source_device,
            virtual_device
        ),
        INPUT_PROXY_ERROR_SOURCE_DISCONNECTED
    );
    failures += expect_no_write("source disconnected", previous_write_calls);

    read_result = INPUT_PROXY_ERROR_EVENT_READ_FAILED;
    previous_write_calls = write_calls;
    failures += expect_result(
        "unrecoverable read failure",
        input_proxy_session_process_event(
            session,
            source_device,
            virtual_device
        ),
        INPUT_PROXY_ERROR_EVENT_READ_FAILED
    );
    failures += expect_no_write(
        "unrecoverable read failure",
        previous_write_calls
    );

    read_result = INPUT_PROXY_SUCCESS;
    write_result = INPUT_PROXY_ERROR_EVENT_WRITE_FAILED;
    source_event = (struct input_event) {
        .type = EV_KEY,
        .code = KEY_B,
        .value = 0
    };
    failures += expect_result(
        "virtual-device write failure",
        input_proxy_session_process_event(
            session,
            source_device,
            virtual_device
        ),
        INPUT_PROXY_ERROR_EVENT_WRITE_FAILED
    );

    if (read_calls != 11 || write_calls != 6) {
        fprintf(
            stderr,
            "unexpected call counts: reads=%d writes=%d\n",
            read_calls,
            write_calls
        );
        failures++;
    }

    input_proxy_session_destroy(session);

    reset_runtime();
    read_results[0] = INPUT_PROXY_SUCCESS;
    read_result_count = 1;
    shutdown_after_write = true;
    failures += run_runtime_test(
        "shutdown while actively forwarding",
        &config,
        INPUT_PROXY_SUCCESS
    );
    if (read_calls != 1 || write_calls != 1 || close_calls != 1 ||
        destroy_calls != 1 || capture_state_calls != 1 ||
        strcmp(barrier_operations, "QBARF") != 0 ||
        strcmp(operations, "DC") != 0) {
        fprintf(stderr, "shutdown while actively forwarding: bad cleanup\n");
        failures++;
    }

    reset_runtime();
    read_results[0] = INPUT_PROXY_EVENT_UNAVAILABLE;
    read_result_count = 1;
    shutdown_during_event_sleep = true;
    failures += run_runtime_test(
        "shutdown while source events unavailable",
        &config,
        INPUT_PROXY_SUCCESS
    );
    if (read_calls != 1 || event_sleep_calls != 1 || close_calls != 1 ||
        destroy_calls != 1 || strcmp(operations, "DC") != 0) {
        fprintf(stderr, "shutdown while source events unavailable: bad cleanup\n");
        failures++;
    }

    reset_runtime();
    open_result = INPUT_PROXY_ERROR_SOURCE_UNAVAILABLE;
    shutdown_during_source_sleep = true;
    failures += run_runtime_test(
        "shutdown while waiting for source",
        &config,
        INPUT_PROXY_SUCCESS
    );
    if (open_calls != 1 || source_sleep_calls != 1 || close_calls != 0 ||
        destroy_calls != 0) {
        fprintf(stderr, "shutdown while waiting for source: bad cleanup\n");
        failures++;
    }

    reset_runtime();
    open_results[0] = INPUT_PROXY_ERROR_SOURCE_UNAVAILABLE;
    open_results[1] = INPUT_PROXY_ERROR_SOURCE_UNAVAILABLE;
    open_results[2] = INPUT_PROXY_ERROR_SOURCE_UNAVAILABLE;
    open_result_count = 3;
    shutdown_after_source_sleep_calls = 3;
    failures += run_runtime_test_with_output(
        "source wait logging",
        &config,
        INPUT_PROXY_SUCCESS,
        output,
        sizeof(output)
    );
    if (count_occurrences(
            output,
            "input-proxy: waiting for source: /dev/input/event-test\n"
        ) != 1 ||
        count_occurrences(output, "shutdown complete") != 1 ||
        strstr(output, "source opened successfully") != NULL) {
        fprintf(stderr, "source wait logging: unexpected output: %s\n", output);
        failures++;
    }

    reset_runtime();
    open_result = INPUT_PROXY_ERROR_SOURCE_OPEN_FAILED;
    failures += run_runtime_test(
        "source open failure",
        &config,
        INPUT_PROXY_ERROR_SOURCE_OPEN_FAILED
    );
    if (open_calls != 1 || create_calls != 0 || close_calls != 0 ||
        destroy_calls != 0) {
        fprintf(stderr, "source open failure: unexpected lifecycle calls\n");
        failures++;
    }

    reset_runtime();
    open_result = INPUT_PROXY_ERROR_SOURCE_PERMISSION_DENIED;
    failures += run_runtime_test(
        "initial source permission denied",
        &config,
        INPUT_PROXY_ERROR_SOURCE_PERMISSION_DENIED
    );
    if (open_calls != 1 || source_sleep_calls != 0 || create_calls != 0 ||
        close_calls != 0 || destroy_calls != 0) {
        fprintf(stderr, "initial source permission denied: unexpected retry\n");
        failures++;
    }

    reset_runtime();
    open_results[0] = INPUT_PROXY_SUCCESS;
    open_results[1] = INPUT_PROXY_ERROR_SOURCE_UNAVAILABLE;
    open_results[2] = INPUT_PROXY_ERROR_SOURCE_PERMISSION_DENIED;
    open_results[3] = INPUT_PROXY_ERROR_SOURCE_PERMISSION_DENIED;
    open_results[4] = INPUT_PROXY_SUCCESS;
    open_result_count = 5;
    compatibility_results[0] = true;
    read_results[0] = INPUT_PROXY_ERROR_SOURCE_DISCONNECTED;
    read_results[1] = INPUT_PROXY_ERROR_EVENT_READ_FAILED;
    read_result_count = 2;
    failures += run_runtime_test(
        "transient reconnect permission denied",
        &config,
        INPUT_PROXY_ERROR_EVENT_READ_FAILED
    );
    if (open_calls != 5 || source_sleep_calls != 3 || create_calls != 1 ||
        compatibility_calls != 1 || neutralize_calls != 1 ||
        destroy_calls != 1 || close_calls != 2 ||
        strcmp(operations, "NCDC") != 0) {
        fprintf(stderr, "transient reconnect permission denied: bad lifecycle\n");
        failures++;
    }

    reset_runtime();
    open_results[0] = INPUT_PROXY_SUCCESS;
    open_result_count = 1;
    open_result = INPUT_PROXY_ERROR_SOURCE_PERMISSION_DENIED;
    read_results[0] = INPUT_PROXY_ERROR_SOURCE_DISCONNECTED;
    read_result_count = 1;
    failures += run_runtime_test_with_output(
        "persistent reconnect permission denied",
        &config,
        INPUT_PROXY_ERROR_SOURCE_PERMISSION_DENIED,
        output,
        sizeof(output)
    );
    if (open_calls != 22 || source_sleep_calls != 20 || create_calls != 1 ||
        neutralize_calls != 1 || destroy_calls != 1 || close_calls != 1 ||
        count_occurrences(output, "waiting for source") > 1 ||
        strcmp(operations, "NCD") != 0) {
        fprintf(stderr, "persistent reconnect permission denied: bad lifecycle\n");
        failures++;
    }

    reset_runtime();
    open_results[0] = INPUT_PROXY_SUCCESS;
    open_result_count = 1;
    open_result = INPUT_PROXY_ERROR_SOURCE_PERMISSION_DENIED;
    read_results[0] = INPUT_PROXY_ERROR_SOURCE_DISCONNECTED;
    read_result_count = 1;
    shutdown_after_source_sleep_calls = 3;
    failures += run_runtime_test(
        "shutdown during reconnect permission settling",
        &config,
        INPUT_PROXY_SUCCESS
    );
    if (open_calls != 4 || source_sleep_calls != 3 || create_calls != 1 ||
        neutralize_calls != 1 || destroy_calls != 1 || close_calls != 1 ||
        strcmp(operations, "NCD") != 0) {
        fprintf(stderr, "shutdown during permission settling: bad lifecycle\n");
        failures++;
    }

    reset_runtime();
    create_result = INPUT_PROXY_ERROR_VIRTUAL_DEVICE_CREATE_FAILED;
    failures += run_runtime_test(
        "virtual device creation failure",
        &config,
        INPUT_PROXY_ERROR_VIRTUAL_DEVICE_CREATE_FAILED
    );
    if (open_calls != 1 || create_calls != 1 || close_calls != 1 ||
        destroy_calls != 0 || strcmp(operations, "C") != 0) {
        fprintf(stderr, "virtual device creation failure: bad cleanup\n");
        failures++;
    }

    reset_runtime();
    read_results[0] = INPUT_PROXY_SUCCESS;
    read_results[1] = INPUT_PROXY_EVENT_UNAVAILABLE;
    read_results[2] = INPUT_PROXY_SUCCESS;
    read_results[3] = INPUT_PROXY_EVENT_SYNC_REQUIRED;
    read_result_count = 4;
    sync_read_results[0] = INPUT_PROXY_SUCCESS;
    sync_read_results[1] = INPUT_PROXY_EVENT_UNAVAILABLE;
    sync_events[0] = (struct input_event) {
        .type = EV_SYN, .code = SYN_REPORT, .value = 0
    };
    sync_read_result_count = 2;
    read_results[4] = INPUT_PROXY_ERROR_EVENT_READ_FAILED;
    read_result_count = 5;
    failures += run_runtime_test(
        "runtime event loop",
        &config,
        INPUT_PROXY_ERROR_EVENT_READ_FAILED
    );
    if (open_calls != 1 || create_calls != 1 || read_calls != 5 ||
        sync_read_calls != 2 || write_calls != 3 ||
        destroy_calls != 1 || close_calls != 1 ||
        event_sleep_calls != 1 || source_sleep_calls != 0 ||
        sleep_duration_failures != 0 ||
        strcmp(operations, "DC") != 0) {
        fprintf(stderr, "runtime event loop: unexpected lifecycle calls\n");
        failures++;
    }

    reset_runtime();
    open_results[0] = INPUT_PROXY_ERROR_SOURCE_UNAVAILABLE;
    open_results[1] = INPUT_PROXY_ERROR_SOURCE_UNAVAILABLE;
    open_results[2] = INPUT_PROXY_SUCCESS;
    open_result_count = 3;
    read_results[0] = INPUT_PROXY_ERROR_EVENT_READ_FAILED;
    read_result_count = 1;
    failures += run_runtime_test(
        "startup source unavailable",
        &config,
        INPUT_PROXY_ERROR_EVENT_READ_FAILED
    );
    if (open_calls != 3 || source_sleep_calls != 2 ||
        event_sleep_calls != 0 || create_calls != 1 ||
        destroy_calls != 1 || close_calls != 1 ||
        sleep_duration_failures != 0 || strcmp(operations, "DC") != 0) {
        fprintf(stderr, "startup source unavailable: bad retry lifecycle\n");
        failures++;
    }

    reset_runtime();
    open_results[0] = INPUT_PROXY_SUCCESS;
    open_results[1] = INPUT_PROXY_ERROR_SOURCE_UNAVAILABLE;
    open_results[2] = INPUT_PROXY_SUCCESS;
    open_results[3] = INPUT_PROXY_ERROR_SOURCE_UNAVAILABLE;
    open_results[4] = INPUT_PROXY_SUCCESS;
    open_result_count = 5;
    compatibility_results[0] = true;
    compatibility_results[1] = true;
    read_results[0] = INPUT_PROXY_SUCCESS;
    read_results[1] = INPUT_PROXY_ERROR_SOURCE_DISCONNECTED;
    read_results[2] = INPUT_PROXY_EVENT_SYNC_REQUIRED;
    read_results[3] = INPUT_PROXY_ERROR_SOURCE_DISCONNECTED;
    read_results[4] = INPUT_PROXY_SUCCESS;
    read_results[5] = INPUT_PROXY_EVENT_UNAVAILABLE;
    read_results[6] = INPUT_PROXY_ERROR_EVENT_READ_FAILED;
    read_result_count = 7;
    sync_read_results[0] = INPUT_PROXY_SUCCESS;
    sync_read_results[1] = INPUT_PROXY_EVENT_UNAVAILABLE;
    sync_events[0] = (struct input_event) {
        .type = EV_SYN, .code = SYN_REPORT, .value = 0
    };
    sync_read_result_count = 2;
    failures += run_runtime_test(
        "disconnect and reconnect",
        &config,
        INPUT_PROXY_ERROR_EVENT_READ_FAILED
    );
    if (open_calls != 5 || create_calls != 1 || compatibility_calls != 2 ||
        read_calls != 7 || write_calls != 3 || destroy_calls != 1 ||
        close_calls != 3 || sync_read_calls != 2 ||
        event_sleep_calls != 1 || source_sleep_calls != 2 ||
        neutralize_calls != 2 || capture_state_calls != 3 ||
        sleep_duration_failures != 0 ||
        strcmp(operations, "NCNCDC") != 0 ||
        strcmp(availability_operations, "TFNCTFNCTCF") != 0) {
        fprintf(stderr, "disconnect and reconnect: bad lifecycle\n");
        failures++;
    }

    reset_runtime();
    open_results[0] = INPUT_PROXY_SUCCESS;
    open_results[1] = INPUT_PROXY_SUCCESS;
    open_result_count = 2;
    compatibility_results[0] = true;
    read_results[0] = INPUT_PROXY_ERROR_SOURCE_DISCONNECTED;
    read_results[1] = INPUT_PROXY_ERROR_EVENT_READ_FAILED;
    read_result_count = 2;
    failures += run_runtime_test_with_output(
        "normal lifecycle logging",
        &config,
        INPUT_PROXY_ERROR_EVENT_READ_FAILED,
        output,
        sizeof(output)
    );
    if (count_occurrences(output, "virtual device created") != 1 ||
        count_occurrences(output, "source connected") != 1 ||
        count_occurrences(output, "source disconnected") != 1 ||
        count_occurrences(output, "source reconnected") != 1 ||
        count_occurrences(output, "/dev/input/event-test") != 3 ||
        count_occurrences(output, "proxy_test_device") != 1 ||
        strstr(output, "source opened successfully") != NULL ||
        strstr(output, "compatible") != NULL ||
        strstr(output, "neutraliz") != NULL ||
        strstr(output, "recovery") != NULL ||
        strstr(output, "type=") != NULL || strstr(output, "code=") != NULL) {
        fprintf(stderr, "normal lifecycle logging: unexpected output: %s\n", output);
        failures++;
    }

    reset_runtime();
    open_results[0] = INPUT_PROXY_SUCCESS;
    open_results[1] = INPUT_PROXY_SUCCESS;
    open_results[2] = INPUT_PROXY_SUCCESS;
    open_result_count = 3;
    compatibility_results[0] = false;
    compatibility_results[1] = false;
    read_results[0] = INPUT_PROXY_ERROR_SOURCE_DISCONNECTED;
    read_results[1] = INPUT_PROXY_ERROR_SOURCE_DISCONNECTED;
    read_results[2] = INPUT_PROXY_ERROR_EVENT_READ_FAILED;
    read_result_count = 3;
    failures += run_runtime_test(
        "incompatible reconnect",
        &config,
        INPUT_PROXY_ERROR_EVENT_READ_FAILED
    );
    if (open_calls != 3 || create_calls != 3 || compatibility_calls != 2 ||
        read_calls != 3 || destroy_calls != 3 || close_calls != 3 ||
        neutralize_calls != 2 || capture_state_calls != 3 ||
        strcmp(operations, "NCDNCDDC") != 0) {
        fprintf(stderr, "incompatible reconnect: bad lifecycle\n");
        failures++;
    }

    reset_runtime();
    open_results[0] = INPUT_PROXY_SUCCESS;
    open_results[1] = INPUT_PROXY_SUCCESS;
    open_result_count = 2;
    compatibility_results[0] = false;
    read_results[0] = INPUT_PROXY_ERROR_SOURCE_DISCONNECTED;
    read_results[1] = INPUT_PROXY_ERROR_EVENT_READ_FAILED;
    read_result_count = 2;
    failures += run_runtime_test_with_output(
        "replacement logging",
        &config,
        INPUT_PROXY_ERROR_EVENT_READ_FAILED,
        output,
        sizeof(output)
    );
    if (count_occurrences(output, "virtual device replaced") != 1 ||
        count_occurrences(output, "source reconnected") != 1) {
        fprintf(stderr, "replacement logging: unexpected output: %s\n", output);
        failures++;
    }

    reset_runtime();
    shutdown_after_write = true;
    failures += run_runtime_test_with_output(
        "verbose lifecycle logging",
        &verbose_config,
        INPUT_PROXY_SUCCESS,
        output,
        sizeof(output)
    );
    if (strstr(output,
            "source opened successfully: /dev/input/event-test") == NULL ||
        strstr(output,
            "creating virtual device proxy_test_device from source "
            "/dev/input/event-test") == NULL ||
        strstr(output,
            "shutdown request handled; cleanup completed for source "
            "/dev/input/event-test and virtual device proxy_test_device") == NULL ||
        strstr(output, "shutdown complete") == NULL ||
        strstr(output, "type=") != NULL || strstr(output, "code=") != NULL) {
        fprintf(stderr, "verbose lifecycle logging: unexpected output: %s\n", output);
        failures++;
    }

    reset_runtime();
    open_results[0] = INPUT_PROXY_SUCCESS;
    open_results[1] = INPUT_PROXY_SUCCESS;
    open_result_count = 2;
    compatibility_results[0] = true;
    read_results[0] = INPUT_PROXY_EVENT_SYNC_REQUIRED;
    read_results[1] = INPUT_PROXY_ERROR_SOURCE_DISCONNECTED;
    read_results[2] = INPUT_PROXY_ERROR_EVENT_READ_FAILED;
    read_result_count = 3;
    sync_read_results[0] = INPUT_PROXY_EVENT_UNAVAILABLE;
    sync_read_result_count = 1;
    failures += run_runtime_test_with_output(
        "verbose recovery logging",
        &verbose_config,
        INPUT_PROXY_ERROR_EVENT_READ_FAILED,
        output,
        sizeof(output)
    );
    if (strstr(output,
            "synchronization recovery started for source "
            "/dev/input/event-test and virtual device proxy_test_device") == NULL ||
        strstr(output,
            "synchronization recovery completed for source "
            "/dev/input/event-test and virtual device proxy_test_device") == NULL ||
        strstr(output,
            "neutralizing virtual device proxy_test_device after loss of "
            "source /dev/input/event-test") == NULL ||
        strstr(output,
            "reconnected source /dev/input/event-test is compatible; "
            "retaining virtual device proxy_test_device") == NULL ||
        strstr(output, "type=") != NULL || strstr(output, "code=") != NULL) {
        fprintf(stderr, "verbose recovery logging: unexpected output: %s\n", output);
        failures++;
    }

    reset_runtime();
    open_results[0] = INPUT_PROXY_SUCCESS;
    open_results[1] = INPUT_PROXY_ERROR_SOURCE_PERMISSION_DENIED;
    open_results[2] = INPUT_PROXY_SUCCESS;
    open_result_count = 3;
    compatibility_results[0] = true;
    read_results[0] = INPUT_PROXY_ERROR_SOURCE_DISCONNECTED;
    read_results[1] = INPUT_PROXY_ERROR_EVENT_READ_FAILED;
    read_result_count = 2;
    failures += run_runtime_test_with_output(
        "verbose permission settling logging",
        &verbose_config,
        INPUT_PROXY_ERROR_EVENT_READ_FAILED,
        output,
        sizeof(output)
    );
    if (count_occurrences(output,
            "permission denied during reconnect; allowing 2 seconds for "
            "permissions to settle") != 1 ||
        strstr(output, "permission settling period expired") != NULL) {
        fprintf(stderr,
            "verbose permission settling logging: unexpected output: %s\n",
            output);
        failures++;
    }

    reset_runtime();
    open_results[0] = INPUT_PROXY_SUCCESS;
    open_results[1] = INPUT_PROXY_SUCCESS;
    open_result_count = 2;
    read_results[0] = INPUT_PROXY_ERROR_SOURCE_DISCONNECTED;
    read_result_count = 1;
    compatibility_results[0] = false;
    fail_create_call = 2;
    failures += expect_result(
        "replacement creation setup",
        input_proxy_session_create(&session, &config),
        INPUT_PROXY_SUCCESS
    );
    if (session != NULL) {
        failures += expect_result(
            "replacement creation failure",
            input_proxy_session_run(session),
            INPUT_PROXY_ERROR_VIRTUAL_DEVICE_CREATE_FAILED
        );
        input_proxy_session_destroy(session);
    }
    if (open_calls != 2 || create_calls != 2 || compatibility_calls != 1 ||
        close_calls != 2 || destroy_calls != 1 ||
        neutralize_calls != 1 || strcmp(operations, "NCDC") != 0) {
        fprintf(stderr, "replacement creation failure: bad lifecycle\n");
        failures++;
    }

    reset_runtime();
    read_results[0] = INPUT_PROXY_ERROR_SOURCE_DISCONNECTED;
    read_result_count = 1;
    neutralize_result = INPUT_PROXY_ERROR_EVENT_READ_FAILED;
    failures += run_runtime_test(
        "neutralization state query failure",
        &config,
        INPUT_PROXY_ERROR_EVENT_READ_FAILED
    );
    if (neutralize_calls != 1 || close_calls != 1 || destroy_calls != 1 ||
        strcmp(operations, "NCD") != 0) {
        fprintf(stderr, "neutralization state query failure: bad lifecycle\n");
        failures++;
    }

    reset_runtime();
    read_results[0] = INPUT_PROXY_ERROR_SOURCE_DISCONNECTED;
    read_result_count = 1;
    neutralize_result = INPUT_PROXY_ERROR_EVENT_WRITE_FAILED;
    failures += run_runtime_test(
        "neutralization write failure",
        &config,
        INPUT_PROXY_ERROR_EVENT_WRITE_FAILED
    );
    if (neutralize_calls != 1 || close_calls != 1 || destroy_calls != 1 ||
        strcmp(operations, "NCD") != 0) {
        fprintf(stderr, "neutralization write failure: bad lifecycle\n");
        failures++;
    }

    reset_runtime();
    capture_state_result = INPUT_PROXY_ERROR_EVENT_READ_FAILED;
    failures += run_runtime_test(
        "startup state query failure",
        &config,
        INPUT_PROXY_ERROR_EVENT_READ_FAILED
    );
    if (capture_state_calls != 1 || state_write_calls != 0 || read_calls != 0 ||
        close_calls != 1 || destroy_calls != 1 ||
        strcmp(operations, "DC") != 0) {
        fprintf(stderr, "startup state query failure: bad lifecycle\n");
        failures++;
    }

    reset_runtime();
    state_write_result = INPUT_PROXY_ERROR_EVENT_WRITE_FAILED;
    failures += run_runtime_test(
        "startup synchronization write failure",
        &config,
        INPUT_PROXY_ERROR_EVENT_WRITE_FAILED
    );
    if (capture_state_calls != 1 || state_write_calls != 1 || read_calls != 0 ||
        close_calls != 1 || destroy_calls != 1 ||
        strcmp(operations, "DC") != 0) {
        fprintf(stderr, "startup synchronization write failure: bad lifecycle\n");
        failures++;
    }

    reset_runtime();
    captured_state_events[0] = (struct input_event) {
        .type = EV_KEY, .code = KEY_A, .value = 1
    };
    captured_state_event_count = 1;
    fail_state_write_call = 2;
    failures += run_runtime_test(
        "startup synchronization boundary failure",
        &config,
        INPUT_PROXY_ERROR_EVENT_WRITE_FAILED
    );
    if (capture_state_calls != 1 || state_write_calls != 2 || read_calls != 0 ||
        close_calls != 1 || destroy_calls != 1 ||
        strcmp(barrier_operations, "QTB") != 0 ||
        strcmp(operations, "DC") != 0) {
        fprintf(stderr, "startup synchronization boundary failure: bad lifecycle\n");
        failures++;
    }

    reset_runtime();
    captured_state_events[0] = (struct input_event) {
        .type = EV_KEY, .code = KEY_A, .value = 1
    };
    captured_state_event_count = 1;
    source_available_result = INPUT_PROXY_ERROR_SOURCE_DISCONNECTED;
    failures += run_runtime_test(
        "source loss during synchronization",
        &config,
        INPUT_PROXY_ERROR_SOURCE_DISCONNECTED
    );
    if (capture_state_calls != 1 || state_write_calls != 2 ||
        source_available_calls != 1 || read_calls != 0 || open_calls != 1 ||
        neutralize_calls != 0 || close_calls != 1 || destroy_calls != 1 ||
        strcmp(barrier_operations, "QTBA") != 0 ||
        strcmp(operations, "DC") != 0) {
        fprintf(stderr, "source loss during synchronization: bad lifecycle\n");
        failures++;
    }

    reset_runtime();
    read_results[0] = INPUT_PROXY_ERROR_EVENT_READ_FAILED;
    read_result_count = 1;
    failures += run_runtime_test(
        "runtime read failure",
        &config,
        INPUT_PROXY_ERROR_EVENT_READ_FAILED
    );

    reset_runtime();
    read_results[0] = INPUT_PROXY_SUCCESS;
    read_result_count = 1;
    write_result = INPUT_PROXY_ERROR_EVENT_WRITE_FAILED;
    failures += run_runtime_test(
        "runtime write failure",
        &config,
        INPUT_PROXY_ERROR_EVENT_WRITE_FAILED
    );

    return failures == 0 ? 0 : 1;
}
