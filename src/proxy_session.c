#include <input_proxy/proxy_session.h>
#include <input_proxy/result.h>
#include <input_proxy/source_device.h>
#include <input_proxy/virtual_device.h>

#include <stdlib.h>
#include <string.h>

struct input_proxy_session {
    char *source_path;
    char *device_name;
    bool verbose;
    bool shutdown_requested;
};

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
    if (session == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    /*
     * TODO: Implement the proxy session lifecycle.
     */

    return INPUT_PROXY_ERROR_NOT_IMPLEMENTED;
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
    if (result != INPUT_PROXY_SUCCESS) {
        return result;
    }

    return input_proxy_virtual_device_write_event(virtual_device, &event);
}

void input_proxy_session_request_shutdown(
    struct input_proxy_session *session)
{
    if (session == NULL) {
        return;
    }

    session->shutdown_requested = true;
}

void input_proxy_session_destroy(
    struct input_proxy_session *session)
{
    if (session == NULL) {
        return;
    }

    free(session->device_name);
    free(session->source_path);
    free(session);
}
