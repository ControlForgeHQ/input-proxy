#ifndef INPUT_PROXY_PROXY_SESSION_INTERNAL_H
#define INPUT_PROXY_PROXY_SESSION_INTERNAL_H

#include <input_proxy/result.h>

#include <stdbool.h>

struct input_proxy_session;
struct input_proxy_source_device;
struct input_proxy_virtual_device;

enum input_proxy_result input_proxy_session_synchronize_state(
    struct input_proxy_session *session,
    const struct input_proxy_source_device *source_device,
    struct input_proxy_virtual_device *virtual_device
);

enum input_proxy_result input_proxy_session_request_paused(
    struct input_proxy_session *session,
    bool paused
);

void input_proxy_session_process_activity_timers(
    struct input_proxy_session *session);

/*
 * Read and process one event from an active source/virtual device pair.
 */
enum input_proxy_result input_proxy_session_process_event(
    struct input_proxy_session *session,
    struct input_proxy_source_device *source_device,
    struct input_proxy_virtual_device *virtual_device
);

#endif
