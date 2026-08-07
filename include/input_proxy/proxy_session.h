#ifndef INPUT_PROXY_PROXY_SESSION_H
#define INPUT_PROXY_PROXY_SESSION_H

#include <input_proxy/result.h>

#include <stdbool.h>

struct input_proxy_session_config {
    const char *source_path;
    const char *device_name;
    bool verbose;
};

struct input_proxy_session;
struct input_proxy_source_device;
struct input_proxy_virtual_device;

enum input_proxy_result input_proxy_session_create(
    struct input_proxy_session **session,
    const struct input_proxy_session_config *config
);

enum input_proxy_result input_proxy_session_run(struct input_proxy_session *session);

/*
 * Read and process one event from an active source/virtual device pair.
 */
enum input_proxy_result input_proxy_session_process_event(
    struct input_proxy_session *session,
    struct input_proxy_source_device *source_device,
    struct input_proxy_virtual_device *virtual_device
);

void input_proxy_session_request_shutdown(struct input_proxy_session *session);

void input_proxy_session_destroy(struct input_proxy_session *session);

#endif
