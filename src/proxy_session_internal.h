#ifndef INPUT_PROXY_PROXY_SESSION_INTERNAL_H
#define INPUT_PROXY_PROXY_SESSION_INTERNAL_H

#include <input_proxy/result.h>

struct input_proxy_session;
struct input_proxy_source_device;
struct input_proxy_virtual_device;

/*
 * Read and process one event from an active source/virtual device pair.
 */
enum input_proxy_result input_proxy_session_process_event(
    struct input_proxy_session *session,
    struct input_proxy_source_device *source_device,
    struct input_proxy_virtual_device *virtual_device
);

#endif
