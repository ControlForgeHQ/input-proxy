#ifndef INPUT_PROXY_RUNTIME_CONTROL_INTERNAL_H
#define INPUT_PROXY_RUNTIME_CONTROL_INTERNAL_H

#include <input_proxy/result.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct input_proxy_runtime_control;

enum input_proxy_runtime_control_failure {
    INPUT_PROXY_RUNTIME_CONTROL_SYSTEM_BUS_UNAVAILABLE,
    INPUT_PROXY_RUNTIME_CONTROL_CONNECTION_REJECTED,
    INPUT_PROXY_RUNTIME_CONTROL_NAME_DENIED,
    INPUT_PROXY_RUNTIME_CONTROL_NAME_OWNED,
    INPUT_PROXY_RUNTIME_CONTROL_INVALID_IDENTIFIER,
    INPUT_PROXY_RUNTIME_CONTROL_INITIALIZATION_FAILED
};

struct input_proxy_runtime_control_state {
    const char *instance_name;
    const char *source_path;
    bool paused;
    bool source_available;
    bool activity_while_running;
    bool activity_while_paused;
};

enum input_proxy_runtime_control_property {
    INPUT_PROXY_RUNTIME_CONTROL_PAUSED = 1U << 0,
    INPUT_PROXY_RUNTIME_CONTROL_SOURCE_AVAILABLE = 1U << 1,
    INPUT_PROXY_RUNTIME_CONTROL_ACTIVITY_WHILE_RUNNING = 1U << 2,
    INPUT_PROXY_RUNTIME_CONTROL_ACTIVITY_WHILE_PAUSED = 1U << 3
};

struct input_proxy_runtime_control_changes {
    unsigned int properties;
    bool paused;
    bool source_available;
    bool activity_while_running;
    bool activity_while_paused;
};

typedef enum input_proxy_result (*input_proxy_runtime_control_pause_handler)(
    void *userdata,
    bool paused);

int input_proxy_runtime_control_derive_service_name(
    char *service_name, size_t service_name_size, const char *instance_name);
enum input_proxy_runtime_control_failure
input_proxy_runtime_control_classify_connection_failure(int error_number);
enum input_proxy_runtime_control_failure
input_proxy_runtime_control_classify_name_failure(int error_number);
struct input_proxy_runtime_control *input_proxy_runtime_control_create(
    const struct input_proxy_runtime_control_state *state,
    input_proxy_runtime_control_pause_handler pause_handler,
    void *pause_handler_userdata);
struct input_proxy_runtime_control *input_proxy_runtime_control_recreate(
    const struct input_proxy_runtime_control_state *state,
    input_proxy_runtime_control_pause_handler pause_handler,
    void *pause_handler_userdata,
    enum input_proxy_runtime_control_failure *failure);
size_t input_proxy_runtime_control_apply_changes(
    struct input_proxy_runtime_control **control,
    struct input_proxy_runtime_control_state *state,
    const struct input_proxy_runtime_control_changes *changes);
void input_proxy_runtime_control_process(struct input_proxy_runtime_control **control);
void input_proxy_runtime_control_wait(
    struct input_proxy_runtime_control **control, uint64_t timeout_usec);
void input_proxy_runtime_control_destroy(struct input_proxy_runtime_control *control);

#endif
