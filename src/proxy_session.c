#define _POSIX_C_SOURCE 200809L

#include <input_proxy/proxy_session.h>
#include <input_proxy/result.h>
#include <input_proxy/source_device.h>
#include <input_proxy/virtual_device.h>

#include "proxy_session_internal.h"
#include "virtual_device_internal.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EVENT_UNAVAILABLE_DELAY_NS 10000000L
#define SOURCE_RETRY_DELAY_NS 100000000L

struct input_proxy_session {
    char *source_path;
    char *device_name;
    struct input_proxy_source_device *source_device;
    struct input_proxy_virtual_device *virtual_device;
    bool verbose;
    volatile sig_atomic_t shutdown_requested;
};

static void log_verbose(
    const struct input_proxy_session *session,
    const char *message)
{
    if (session->verbose) {
        printf("input-proxy: %s\n", message);
        fflush(stdout);
    }
}

static void cleanup_active_devices(struct input_proxy_session *session)
{
    input_proxy_virtual_device_destroy(session->virtual_device);
    session->virtual_device = NULL;

    input_proxy_source_device_close(session->source_device);
    session->source_device = NULL;
}

static void close_source_device(struct input_proxy_session *session)
{
    input_proxy_source_device_close(session->source_device);
    session->source_device = NULL;
}

static void wait_for_event(void)
{
    const struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = EVENT_UNAVAILABLE_DELAY_NS
    };

    (void)nanosleep(&delay, NULL);
}

static void wait_for_source(void)
{
    const struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = SOURCE_RETRY_DELAY_NS
    };

    (void)nanosleep(&delay, NULL);
}

static enum input_proxy_result create_active_devices(
    struct input_proxy_session *session)
{
    enum input_proxy_result result;
    bool waiting_logged = false;
    bool replacing_virtual_device;

    while (!session->shutdown_requested) {
        result = input_proxy_source_device_open(
            &session->source_device,
            session->source_path
        );
        if (result == INPUT_PROXY_ERROR_SOURCE_UNAVAILABLE) {
            if (!waiting_logged) {
                printf(
                    "input-proxy: waiting for source %s\n",
                    session->source_path
                );
                fflush(stdout);
                waiting_logged = true;
            }
            wait_for_source();
            continue;
        }
        if (result != INPUT_PROXY_SUCCESS) {
            return result;
        }

        log_verbose(session, "source opened successfully");

        if (session->virtual_device != NULL &&
            input_proxy_virtual_device_is_compatible(
                session->virtual_device,
                session->source_device
            )) {
            log_verbose(
                session,
                "reconnected source is compatible; retaining virtual device"
            );
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
                "reconnected source is incompatible; replacing virtual device"
            );
        }

        input_proxy_virtual_device_destroy(session->virtual_device);
        session->virtual_device = NULL;

        result = input_proxy_virtual_device_create(
            &session->virtual_device,
            session->source_device,
            session->device_name
        );
        if (result != INPUT_PROXY_SUCCESS) {
            close_source_device(session);
            return result;
        }

        if (replacing_virtual_device) {
            printf(
                "input-proxy: virtual device replaced: %s\n",
                session->device_name
            );
            printf(
                "input-proxy: source reconnected: %s\n",
                session->source_path
            );
            fflush(stdout);
        } else {
            printf(
                "input-proxy: virtual device created: %s\n",
                session->device_name
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
    (void)session;

    return input_proxy_virtual_device_write_event(virtual_device, event);
}

static enum input_proxy_result recover_synchronization(
    struct input_proxy_session *session,
    struct input_proxy_source_device *source_device,
    struct input_proxy_virtual_device *virtual_device)
{
    struct input_event event;
    enum input_proxy_result result;

    log_verbose(session, "synchronization recovery started");

    for (;;) {
        result = input_proxy_source_device_read_sync_event(
            source_device,
            &event
        );
        if (result == INPUT_PROXY_EVENT_UNAVAILABLE) {
            log_verbose(session, "synchronization recovery completed");
            return INPUT_PROXY_SUCCESS;
        }
        if (result != INPUT_PROXY_SUCCESS) {
            return result;
        }

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

    if (config->source_path == NULL || config->device_name == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    *session = NULL;

    new_session = calloc(1, sizeof(*new_session));
    if (new_session == NULL) {
        return INPUT_PROXY_ERROR_OUT_OF_MEMORY;
    }

    new_session->source_path = duplicate_string(config->source_path);
    if (new_session->source_path == NULL) {
        result = INPUT_PROXY_ERROR_OUT_OF_MEMORY;
        goto error;
    }

    new_session->device_name = duplicate_string(config->device_name);
    if (new_session->device_name == NULL) {
        result = INPUT_PROXY_ERROR_OUT_OF_MEMORY;
        goto error;
    }

    new_session->verbose = config->verbose;

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

    result = INPUT_PROXY_SUCCESS;
    while (!session->shutdown_requested) {
        result = create_active_devices(session);
        if (result != INPUT_PROXY_SUCCESS || session->shutdown_requested) {
            break;
        }

        while (!session->shutdown_requested) {
            result = input_proxy_session_process_event(
                session,
                session->source_device,
                session->virtual_device
            );
            if (result == INPUT_PROXY_EVENT_UNAVAILABLE) {
                wait_for_event();
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
            printf(
                "input-proxy: source disconnected: %s\n",
                session->source_path
            );
            fflush(stdout);
            log_verbose(
                session,
                "neutralizing persistent virtual device after source loss"
            );
            result = input_proxy_virtual_device_neutralize(
                session->virtual_device
            );
            close_source_device(session);
            if (result != INPUT_PROXY_SUCCESS) {
                break;
            }
            log_verbose(
                session,
                "source-loss neutralization completed; retaining virtual device"
            );
            continue;
        }

        break;
    }

    cleanup_active_devices(session);

    if (session->shutdown_requested) {
        log_verbose(session, "shutdown request handled; cleanup completed");
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
        return recover_synchronization(
            session,
            source_device,
            virtual_device
        );
    }
    if (result != INPUT_PROXY_SUCCESS) {
        return result;
    }

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
    free(session->device_name);
    free(session->source_path);
    free(session);
}
