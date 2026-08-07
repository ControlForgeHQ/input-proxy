#include <input_proxy/proxy_session.h>
#include <input_proxy/source_device.h>
#include <input_proxy/virtual_device.h>

#include "proxy_session_internal.h"

#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static enum input_proxy_result read_result;
static enum input_proxy_result write_result;
static enum input_proxy_result open_result;
static enum input_proxy_result create_result;
static enum input_proxy_result neutralize_result;
static int fail_create_call;
static bool compatibility_results[8];
static enum input_proxy_result open_results[8];
static enum input_proxy_result read_results[8];
static enum input_proxy_result sync_read_results[8];
static struct input_event sync_events[8];
static struct input_event source_event;
static struct input_event written_events[8];
static char operations[16];
static int operation_count;
static int open_calls;
static int open_result_count;
static int create_calls;
static int compatibility_calls;
static int close_calls;
static int destroy_calls;
static int neutralize_calls;
static int read_calls;
static int write_calls;
static int read_result_count;
static int sync_read_calls;
static int sync_read_result_count;
static int event_sleep_calls;
static int source_sleep_calls;
static int sleep_duration_failures;

static struct input_proxy_source_device *const test_source_device =
    (struct input_proxy_source_device *)1;
static struct input_proxy_virtual_device *const test_virtual_device =
    (struct input_proxy_virtual_device *)1;

int nanosleep(const struct timespec *duration, struct timespec *remaining)
{
    (void)remaining;
    if (duration->tv_sec == 0 && duration->tv_nsec == 10000000L) {
        event_sleep_calls++;
    } else if (duration->tv_sec == 0 && duration->tv_nsec == 100000000L) {
        source_sleep_calls++;
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
    }
}

enum input_proxy_result input_proxy_virtual_device_create(
    struct input_proxy_virtual_device **device,
    const struct input_proxy_source_device *source_device,
    const char *device_name)
{
    create_calls++;
    if (source_device != test_source_device ||
        strcmp(device_name, "proxy test device") != 0) {
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

    if (write_calls < 8) {
        written_events[write_calls] = *event;
    }
    write_calls++;

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
    read_calls = 0;
    write_calls = 0;
    read_result_count = 0;
    sync_read_calls = 0;
    sync_read_result_count = 0;
    event_sleep_calls = 0;
    source_sleep_calls = 0;
    sleep_duration_failures = 0;
    memset(operations, 0, sizeof(operations));
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

    failures += expect_result(
        test_name,
        input_proxy_session_run(session),
        expected
    );
    input_proxy_session_destroy(session);
    return failures;
}

int main(void)
{
    const struct input_proxy_session_config config = {
        .source_path = "/dev/input/event-test",
        .device_name = "proxy test device",
        .verbose = false
    };
    struct input_proxy_source_device *const source_device =
        (struct input_proxy_source_device *)1;
    struct input_proxy_virtual_device *const virtual_device =
        (struct input_proxy_virtual_device *)1;
    struct input_proxy_session *session;
    int previous_write_calls;
    int failures = 0;

    reset_runtime();

    failures += expect_result(
        "session creation",
        input_proxy_session_create(&session, &config),
        INPUT_PROXY_SUCCESS
    );
    if (session == NULL) {
        return 1;
    }

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
        neutralize_calls != 2 || sleep_duration_failures != 0 ||
        strcmp(operations, "NCNCDC") != 0) {
        fprintf(stderr, "disconnect and reconnect: bad lifecycle\n");
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
        neutralize_calls != 2 || strcmp(operations, "NCDNCDDC") != 0) {
        fprintf(stderr, "incompatible reconnect: bad lifecycle\n");
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
