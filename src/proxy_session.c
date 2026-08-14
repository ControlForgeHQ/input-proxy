#define _POSIX_C_SOURCE 200809L

#include <input_proxy/proxy_session.h>
#include <input_proxy/result.h>
#include <input_proxy/source_device.h>
#include <input_proxy/virtual_device.h>

#include "proxy_session_internal.h"
#include "instance_name_internal.h"
#include "runtime_control_internal.h"
#include "source_device_internal.h"
#include "virtual_device_internal.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EVENT_UNAVAILABLE_DELAY_NS 10000000L
#define SOURCE_RETRY_DELAY_NS 100000000L
#define RECONNECT_SETTLING_WINDOW_SECONDS 2
#define NANOSECONDS_PER_MILLISECOND UINT64_C(1000000)
#define MICROSECONDS_PER_SECOND UINT64_C(1000000)

struct input_proxy_session {
    char *source_path;
    char *instance_name;
    struct input_proxy_instance_name *name_ownership;
    struct input_proxy_source_device *source_device;
    struct input_proxy_virtual_device *virtual_device;
    struct input_proxy_runtime_control *runtime_control;
    struct input_proxy_runtime_control_state runtime_state;
    struct timespec running_activity_deadline;
    struct timespec paused_activity_deadline;
    uint64_t activity_timeout_ms;
    uint64_t detection_throttle_ms;
    bool activity_tracking_available;
    bool running_activity_timer_active;
    bool paused_activity_timer_active;
    enum input_proxy_result fatal_result;
    bool source_opened_successfully;
    bool verbose;
    volatile sig_atomic_t shutdown_requested;
};

static bool monotonic_now(struct timespec *now)
{
    return clock_gettime(CLOCK_MONOTONIC, now) == 0;
}

static bool deadline_expired(const struct timespec *deadline,
    const struct timespec *now)
{
    return now->tv_sec > deadline->tv_sec ||
        (now->tv_sec == deadline->tv_sec && now->tv_nsec >= deadline->tv_nsec);
}

static void set_deadline(struct timespec *deadline,
    const struct timespec *now, uint64_t duration_ms)
{
    const uint64_t duration_ns = duration_ms * NANOSECONDS_PER_MILLISECOND;
    const uint64_t seconds = duration_ns / UINT64_C(1000000000);
    const uint64_t nanoseconds = duration_ns % UINT64_C(1000000000);

    deadline->tv_sec = now->tv_sec + (time_t)seconds;
    deadline->tv_nsec = now->tv_nsec + (long)nanoseconds;
    if (deadline->tv_nsec >= 1000000000L) {
        deadline->tv_sec++;
        deadline->tv_nsec -= 1000000000L;
    }
}

static void discard_activity_tracking(struct input_proxy_session *session)
{
    const struct input_proxy_runtime_control_changes changes = {
        .properties = INPUT_PROXY_RUNTIME_CONTROL_ACTIVITY_WHILE_RUNNING |
            INPUT_PROXY_RUNTIME_CONTROL_ACTIVITY_WHILE_PAUSED,
        .activity_while_running = false,
        .activity_while_paused = false
    };

    session->activity_tracking_available = false;
    session->running_activity_timer_active = false;
    session->paused_activity_timer_active = false;
    input_proxy_runtime_control_apply_changes(
        NULL, &session->runtime_state, &changes);
}

static void update_control_availability(struct input_proxy_session *session)
{
    if (session->runtime_control == NULL) {
        if (session->activity_tracking_available) {
            discard_activity_tracking(session);
        }
    } else if (!session->activity_tracking_available) {
        session->activity_tracking_available = true;
    }
}

static void process_physical_activity(struct input_proxy_session *session)
{
    struct input_proxy_runtime_control_changes changes = {0};
    struct timespec now;
    uint64_t duration_ms;

    update_control_availability(session);
    if (!session->activity_tracking_available || !monotonic_now(&now)) {
        return;
    }

    if (session->runtime_state.paused) {
        if (session->paused_activity_timer_active) {
            return;
        }
        duration_ms = session->detection_throttle_ms;
        changes.properties =
            INPUT_PROXY_RUNTIME_CONTROL_ACTIVITY_WHILE_PAUSED;
        changes.activity_while_paused = duration_ms != 0;
        session->paused_activity_timer_active = duration_ms != 0;
        if (session->paused_activity_timer_active) {
            set_deadline(&session->paused_activity_deadline, &now, duration_ms);
        }
    } else {
        duration_ms = session->activity_timeout_ms;
        changes.properties =
            INPUT_PROXY_RUNTIME_CONTROL_ACTIVITY_WHILE_RUNNING;
        changes.activity_while_running = duration_ms != 0;
        session->running_activity_timer_active = duration_ms != 0;
        if (session->running_activity_timer_active) {
            set_deadline(&session->running_activity_deadline, &now, duration_ms);
        }
    }
    input_proxy_runtime_control_apply_changes(
        &session->runtime_control, &session->runtime_state, &changes);
    update_control_availability(session);
}

void input_proxy_session_process_activity_timers(
    struct input_proxy_session *session)
{
    struct input_proxy_runtime_control_changes changes = {0};
    struct timespec now;

    if (session == NULL) {
        return;
    }
    update_control_availability(session);
    if (!session->activity_tracking_available || !monotonic_now(&now)) {
        return;
    }
    if (session->running_activity_timer_active &&
        deadline_expired(&session->running_activity_deadline, &now)) {
        session->running_activity_timer_active = false;
        changes.properties |=
            INPUT_PROXY_RUNTIME_CONTROL_ACTIVITY_WHILE_RUNNING;
    }
    if (session->paused_activity_timer_active &&
        deadline_expired(&session->paused_activity_deadline, &now)) {
        session->paused_activity_timer_active = false;
        changes.properties |= INPUT_PROXY_RUNTIME_CONTROL_ACTIVITY_WHILE_PAUSED;
    }
    if (changes.properties != 0U) {
        input_proxy_runtime_control_apply_changes(
            &session->runtime_control, &session->runtime_state, &changes);
        update_control_availability(session);
    }
}

static void set_source_available(
    struct input_proxy_session *session,
    bool source_available)
{
    const struct input_proxy_runtime_control_changes changes = {
        .properties = INPUT_PROXY_RUNTIME_CONTROL_SOURCE_AVAILABLE,
        .source_available = source_available
    };

    input_proxy_runtime_control_apply_changes(
        &session->runtime_control,
        &session->runtime_state,
        &changes
    );
    update_control_availability(session);
}

static enum input_proxy_result handle_pause_request(void *userdata, bool paused)
{
    return input_proxy_session_request_paused(userdata, paused);
}

static void log_verbose(
    const struct input_proxy_session *session,
    const char *format,
    ...)
{
    if (session->verbose) {
        va_list arguments;

        fputs("input-proxy: ", stdout);
        va_start(arguments, format);
        vprintf(format, arguments);
        va_end(arguments);
        fputc('\n', stdout);
        fflush(stdout);
    }
}

static void cleanup_active_devices(struct input_proxy_session *session)
{
    input_proxy_virtual_device_destroy(session->virtual_device);
    session->virtual_device = NULL;

    input_proxy_source_device_close(session->source_device);
    session->source_device = NULL;
    set_source_available(session, false);
}

static void close_source_device(struct input_proxy_session *session)
{
    input_proxy_source_device_close(session->source_device);
    session->source_device = NULL;
    set_source_available(session, false);
}

static uint64_t activity_wait_timeout_usec(
    struct input_proxy_session *session, uint64_t maximum_usec)
{
    const struct timespec *deadline = NULL;
    struct timespec now;
    uint64_t remaining_usec;
    time_t seconds;
    long nanoseconds;

    input_proxy_session_process_activity_timers(session);
    if (session->running_activity_timer_active) {
        deadline = &session->running_activity_deadline;
    } else if (session->paused_activity_timer_active) {
        deadline = &session->paused_activity_deadline;
    }
    if (deadline == NULL || !monotonic_now(&now)) {
        return maximum_usec;
    }
    if (deadline_expired(deadline, &now)) {
        return 0;
    }
    seconds = deadline->tv_sec - now.tv_sec;
    nanoseconds = deadline->tv_nsec - now.tv_nsec;
    if (nanoseconds < 0) {
        seconds--;
        nanoseconds += 1000000000L;
    }
    remaining_usec = (uint64_t)seconds * MICROSECONDS_PER_SECOND +
        ((uint64_t)nanoseconds + UINT64_C(999)) / UINT64_C(1000);
    return remaining_usec < maximum_usec ? remaining_usec : maximum_usec;
}

static void wait_with_activity_timer(
    struct input_proxy_session *session, uint64_t maximum_usec)
{
    input_proxy_runtime_control_wait(
        &session->runtime_control,
        activity_wait_timeout_usec(session, maximum_usec));
    update_control_availability(session);
    input_proxy_session_process_activity_timers(session);
}

static void wait_for_event(struct input_proxy_session *session)
{
    const struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = EVENT_UNAVAILABLE_DELAY_NS
    };

    wait_with_activity_timer(session, (uint64_t)delay.tv_nsec / 1000U);
}

static void wait_for_source(struct input_proxy_session *session)
{
    const struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = SOURCE_RETRY_DELAY_NS
    };

    wait_with_activity_timer(session, (uint64_t)delay.tv_nsec / 1000U);
}

static bool reconnect_settling_expired(const struct timespec *deadline)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return true;
    }

    return now.tv_sec > deadline->tv_sec ||
        (now.tv_sec == deadline->tv_sec && now.tv_nsec >= deadline->tv_nsec);
}

static enum input_proxy_result create_active_devices(
    struct input_proxy_session *session)
{
    enum input_proxy_result result;
    bool waiting_logged = false;
    bool replacing_virtual_device;
    bool permission_settling = false;
    struct timespec permission_deadline;

    while (!session->shutdown_requested) {
        input_proxy_runtime_control_process(&session->runtime_control);
        update_control_availability(session);
        input_proxy_session_process_activity_timers(session);
        if (session->fatal_result != INPUT_PROXY_SUCCESS) {
            return session->fatal_result;
        }
        result = input_proxy_source_device_open(
            &session->source_device,
            session->source_path
        );
        if (result == INPUT_PROXY_ERROR_SOURCE_UNAVAILABLE) {
            if (!waiting_logged) {
                printf(
                    "input-proxy: waiting for source: %s\n",
                    session->source_path
                );
                fflush(stdout);
                waiting_logged = true;
            }
            wait_for_source(session);
            continue;
        }
        if (result == INPUT_PROXY_ERROR_SOURCE_PERMISSION_DENIED &&
            session->source_opened_successfully) {
            if (!permission_settling) {
                if (clock_gettime(CLOCK_MONOTONIC, &permission_deadline) != 0) {
                    return result;
                }
                permission_deadline.tv_sec +=
                    RECONNECT_SETTLING_WINDOW_SECONDS;
                permission_settling = true;
                log_verbose(
                    session,
                    "source %s permission denied during reconnect; "
                    "allowing %d seconds for permissions to settle",
                    session->source_path,
                    RECONNECT_SETTLING_WINDOW_SECONDS
                );
            } else if (reconnect_settling_expired(&permission_deadline)) {
                log_verbose(
                    session,
                    "source %s permission settling period expired",
                    session->source_path
                );
                return result;
            }

            wait_for_source(session);
            continue;
        }
        if (result != INPUT_PROXY_SUCCESS) {
            return result;
        }

        session->source_opened_successfully = true;
        set_source_available(session, true);
        log_verbose(
            session,
            "source opened successfully: %s",
            session->source_path
        );

        if (session->virtual_device != NULL &&
            input_proxy_virtual_device_is_compatible(
                session->virtual_device,
                session->source_device
            )) {
            log_verbose(
                session,
                "reconnected source %s is compatible; retaining virtual "
                "device %s",
                session->source_path,
                session->instance_name
            );
            if (!session->runtime_state.paused) {
                result = input_proxy_session_synchronize_state(
                    session,
                    session->source_device,
                    session->virtual_device
                );
                if (result != INPUT_PROXY_SUCCESS) {
                    return result;
                }
            }
            printf(
                "input-proxy: source reconnected: %s\n",
                session->source_path
            );
            fflush(stdout);
            return INPUT_PROXY_SUCCESS;
        }

        replacing_virtual_device = session->virtual_device != NULL;
        if (replacing_virtual_device) {
            log_verbose(
                session,
                "reconnected source %s is incompatible; replacing virtual "
                "device %s",
                session->source_path,
                session->instance_name
            );
        } else {
            log_verbose(
                session,
                "creating virtual device %s from source %s",
                session->instance_name,
                session->source_path
            );
        }

        input_proxy_virtual_device_destroy(session->virtual_device);
        session->virtual_device = NULL;

        result = input_proxy_virtual_device_create(
            &session->virtual_device,
            session->source_device,
            session->instance_name
        );
        if (result != INPUT_PROXY_SUCCESS) {
            close_source_device(session);
            return result;
        }

        if (!session->runtime_state.paused) {
            result = input_proxy_session_synchronize_state(
                session,
                session->source_device,
                session->virtual_device
            );
            if (result != INPUT_PROXY_SUCCESS) {
                return result;
            }
        }

        if (replacing_virtual_device) {
            printf(
                "input-proxy: virtual device replaced: %s\n",
                session->instance_name
            );
            printf(
                "input-proxy: source reconnected: %s\n",
                session->source_path
            );
            fflush(stdout);
        } else {
            printf(
                "input-proxy: virtual device created: %s\n",
                session->instance_name
            );
            printf(
                "input-proxy: source connected: %s\n",
                session->source_path
            );
            fflush(stdout);
        }

        return INPUT_PROXY_SUCCESS;
    }

    return INPUT_PROXY_SUCCESS;
}

static char *duplicate_string(const char *source)
{
    size_t length;
    char *copy;

    if (source == NULL) {
        return NULL;
    }

    length = strlen(source) + 1;

    copy = malloc(length);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, source, length);

    return copy;
}

static enum input_proxy_result process_read_event(
    struct input_proxy_session *session,
    struct input_proxy_virtual_device *virtual_device,
    const struct input_event *event)
{
    if (session->runtime_state.paused) {
        return INPUT_PROXY_SUCCESS;
    }

    return input_proxy_virtual_device_write_event(virtual_device, event);
}

enum input_proxy_result input_proxy_session_synchronize_state(
    struct input_proxy_session *session,
    const struct input_proxy_source_device *source_device,
    struct input_proxy_virtual_device *virtual_device)
{
    const struct input_event synchronization_boundary = {
        .type = EV_SYN,
        .code = SYN_REPORT,
        .value = 0
    };
    struct input_proxy_source_state state = {0};
    enum input_proxy_result result;
    size_t index;

    if (session == NULL || source_device == NULL || virtual_device == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    log_verbose(
        session,
        "synchronizing virtual device %s to current state of source %s",
        session->instance_name,
        session->source_path
    );

    result = input_proxy_source_device_capture_state(source_device, &state);
    if (result != INPUT_PROXY_SUCCESS) {
        goto cleanup;
    }

    for (index = 0; index < state.event_count; index++) {
        result = input_proxy_virtual_device_write_event(
            virtual_device,
            &state.events[index]
        );
        if (result != INPUT_PROXY_SUCCESS) {
            goto cleanup;
        }
    }

    result = input_proxy_virtual_device_write_event(
        virtual_device,
        &synchronization_boundary
    );
    if (result == INPUT_PROXY_SUCCESS) {
        result = input_proxy_source_device_check_available(source_device);
    }
    if (result == INPUT_PROXY_SUCCESS) {
        log_verbose(
            session,
            "current-state synchronization completed for source %s and "
            "virtual device %s",
            session->source_path,
            session->instance_name
        );
    }

cleanup:
    input_proxy_source_state_destroy(&state);
    if (result != INPUT_PROXY_SUCCESS) {
        fprintf(
            stderr,
            "input-proxy: fatal current-state synchronization failed for "
            "virtual device %s and source %s: %s\n",
            session->instance_name,
            session->source_path,
            input_proxy_result_string(result)
        );
    }
    return result;
}

enum input_proxy_result input_proxy_session_request_paused(
    struct input_proxy_session *session,
    bool paused)
{
    struct input_proxy_runtime_control_changes changes;
    enum input_proxy_result result = INPUT_PROXY_SUCCESS;

    if (session == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }
    if (session->runtime_state.paused == paused) {
        return INPUT_PROXY_SUCCESS;
    }

    printf("input-proxy: %s requested\n", paused ? "pause" : "resume");
    fflush(stdout);

    if (session->runtime_state.source_available) {
        if (paused) {
            log_verbose(
                session,
                "neutralizing virtual device %s before paused operation",
                session->instance_name
            );
            result = input_proxy_virtual_device_neutralize(
                session->virtual_device);
        } else {
            result = input_proxy_session_synchronize_state(
                session, session->source_device, session->virtual_device);
        }
    }
    if (result != INPUT_PROXY_SUCCESS) {
        session->fatal_result = result;
        return result;
    }

    changes = (struct input_proxy_runtime_control_changes) {
        .properties = INPUT_PROXY_RUNTIME_CONTROL_PAUSED |
            (paused
                ? INPUT_PROXY_RUNTIME_CONTROL_ACTIVITY_WHILE_RUNNING
                : INPUT_PROXY_RUNTIME_CONTROL_ACTIVITY_WHILE_PAUSED),
        .paused = paused,
        .activity_while_running = false,
        .activity_while_paused = false
    };
    if (paused) {
        session->running_activity_timer_active = false;
    } else {
        session->paused_activity_timer_active = false;
    }
    input_proxy_runtime_control_apply_changes(
        &session->runtime_control, &session->runtime_state, &changes);
    update_control_availability(session);
    if (session->runtime_state.source_available) {
        log_verbose(
            session,
            paused
                ? "forwarding suppressed; continuing to consume source %s"
                : "ordinary forwarding enabled for source %s",
            session->source_path
        );
    } else {
        log_verbose(
            session,
            "%s state committed without immediate correction because source "
            "%s is unavailable",
            paused ? "paused" : "unpaused",
            session->source_path
        );
    }
    return INPUT_PROXY_SUCCESS;
}

static enum input_proxy_result recover_synchronization(
    struct input_proxy_session *session,
    struct input_proxy_source_device *source_device,
    struct input_proxy_virtual_device *virtual_device)
{
    struct input_event event;
    enum input_proxy_result result;

    log_verbose(
        session,
        "synchronization recovery started for source %s and virtual device %s",
        session->source_path,
        session->instance_name
    );

    for (;;) {
        result = input_proxy_source_device_read_sync_event(
            source_device,
            &event
        );
        if (result == INPUT_PROXY_EVENT_UNAVAILABLE) {
            log_verbose(
                session,
                "synchronization recovery completed for source %s and "
                "virtual device %s",
                session->source_path,
                session->instance_name
            );
            return INPUT_PROXY_SUCCESS;
        }
        if (result != INPUT_PROXY_SUCCESS) {
            return result;
        }

        process_physical_activity(session);

        result = process_read_event(
            session,
            virtual_device,
            &event
        );
        if (result != INPUT_PROXY_SUCCESS) {
            return result;
        }
    }
}

enum input_proxy_result input_proxy_session_create(
    struct input_proxy_session **session,
    const struct input_proxy_session_config *config)
{
    struct input_proxy_session *new_session;
    enum input_proxy_result result;

    if (session == NULL || config == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    if (config->source_path == NULL || config->instance_name == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    *session = NULL;

    result = input_proxy_instance_name_validate(config->instance_name);
    if (result != INPUT_PROXY_SUCCESS) {
        return result;
    }

    new_session = calloc(1, sizeof(*new_session));
    if (new_session == NULL) {
        return INPUT_PROXY_ERROR_OUT_OF_MEMORY;
    }

    new_session->source_path = duplicate_string(config->source_path);
    if (new_session->source_path == NULL) {
        result = INPUT_PROXY_ERROR_OUT_OF_MEMORY;
        goto error;
    }

    new_session->instance_name = duplicate_string(config->instance_name);
    if (new_session->instance_name == NULL) {
        result = INPUT_PROXY_ERROR_OUT_OF_MEMORY;
        goto error;
    }

    result = input_proxy_instance_name_acquire(
        &new_session->name_ownership,
        new_session->instance_name
    );
    if (result != INPUT_PROXY_SUCCESS) {
        goto error;
    }

    new_session->verbose = config->verbose;
    new_session->activity_timeout_ms = config->activity_timeout_ms;
    new_session->detection_throttle_ms = config->detection_throttle_ms;
    new_session->runtime_state = (struct input_proxy_runtime_control_state) {
        .instance_name = new_session->instance_name,
        .source_path = new_session->source_path,
        .paused = false,
        .source_available = false,
        .activity_while_running = false,
        .activity_while_paused = false
    };

    *session = new_session;

    return INPUT_PROXY_SUCCESS;

error:
    input_proxy_session_destroy(new_session);
    return result;
}

enum input_proxy_result input_proxy_session_run(
    struct input_proxy_session *session)
{
    enum input_proxy_result result;

    if (session == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    session->runtime_control = input_proxy_runtime_control_create(
        &session->runtime_state,
        handle_pause_request,
        session
    );
    session->activity_tracking_available = session->runtime_control != NULL;
    result = INPUT_PROXY_SUCCESS;
    while (!session->shutdown_requested) {
        result = create_active_devices(session);
        if (result != INPUT_PROXY_SUCCESS || session->shutdown_requested) {
            break;
        }

        while (!session->shutdown_requested) {
            input_proxy_runtime_control_process(&session->runtime_control);
            update_control_availability(session);
            input_proxy_session_process_activity_timers(session);
            if (session->fatal_result != INPUT_PROXY_SUCCESS) {
                result = session->fatal_result;
                break;
            }
            result = input_proxy_session_process_event(
                session,
                session->source_device,
                session->virtual_device
            );
            if (result == INPUT_PROXY_EVENT_UNAVAILABLE) {
                wait_for_event(session);
                continue;
            }

            if (result == INPUT_PROXY_SUCCESS) {
                continue;
            }

            break;
        }

        if (session->shutdown_requested &&
            (result == INPUT_PROXY_SUCCESS ||
             result == INPUT_PROXY_EVENT_UNAVAILABLE)) {
            result = INPUT_PROXY_SUCCESS;
            break;
        }

        if (result == INPUT_PROXY_ERROR_SOURCE_DISCONNECTED) {
            set_source_available(session, false);
            printf(
                "input-proxy: source disconnected: %s\n",
                session->source_path
            );
            fflush(stdout);
            log_verbose(
                session,
                "neutralizing virtual device %s after loss of source %s",
                session->instance_name,
                session->source_path
            );
            result = input_proxy_virtual_device_neutralize(
                session->virtual_device
            );
            close_source_device(session);
            if (result != INPUT_PROXY_SUCCESS) {
                fprintf(
                    stderr,
                    "input-proxy: fatal source-loss neutralization failed "
                    "for virtual device %s after loss of source %s: %s\n",
                    session->instance_name,
                    session->source_path,
                    input_proxy_result_string(result)
                );
                break;
            }
            log_verbose(
                session,
                "source-loss neutralization completed; retaining virtual "
                "device %s while waiting for source %s",
                session->instance_name,
                session->source_path
            );
            continue;
        }

        break;
    }

    cleanup_active_devices(session);
    input_proxy_runtime_control_destroy(session->runtime_control);
    session->runtime_control = NULL;

    if (session->shutdown_requested) {
        log_verbose(
            session,
            "shutdown request handled; cleanup completed for source %s and "
            "virtual device %s",
            session->source_path,
            session->instance_name
        );
        printf("input-proxy: shutdown complete\n");
        fflush(stdout);
    }

    return result;
}

enum input_proxy_result input_proxy_session_process_event(
    struct input_proxy_session *session,
    struct input_proxy_source_device *source_device,
    struct input_proxy_virtual_device *virtual_device)
{
    struct input_event event;
    enum input_proxy_result result;

    if (session == NULL || source_device == NULL || virtual_device == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    result = input_proxy_source_device_read_event(source_device, &event);
    if (result == INPUT_PROXY_EVENT_SYNC_REQUIRED) {
        process_physical_activity(session);
        return recover_synchronization(
            session,
            source_device,
            virtual_device
        );
    }
    if (result != INPUT_PROXY_SUCCESS) {
        return result;
    }

    process_physical_activity(session);

    return process_read_event(session, virtual_device, &event);
}

void input_proxy_session_request_shutdown(
    struct input_proxy_session *session)
{
    if (session == NULL) {
        return;
    }

    session->shutdown_requested = 1;
}

void input_proxy_session_destroy(
    struct input_proxy_session *session)
{
    if (session == NULL) {
        return;
    }

    cleanup_active_devices(session);
    input_proxy_runtime_control_destroy(session->runtime_control);
    input_proxy_instance_name_release(session->name_ownership);
    free(session->instance_name);
    free(session->source_path);
    free(session);
}
