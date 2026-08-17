#ifndef INPUT_PROXY_RUNTIME_POLICY_INTERNAL_H
#define INPUT_PROXY_RUNTIME_POLICY_INTERNAL_H

#include <input_proxy/proxy_session.h>

#include <stdbool.h>
#include <stdint.h>

void input_proxy_runtime_policy_defaults(
    struct input_proxy_session_config *config
);

bool input_proxy_runtime_policy_parse_duration(
    const char *text,
    uint64_t *duration_ms
);

bool input_proxy_runtime_policy_parse_on_off(
    const char *text,
    bool *enabled
);

bool input_proxy_runtime_policy_is_valid(
    const struct input_proxy_session_config *config
);

#endif
