#include "runtime_policy_internal.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

void input_proxy_runtime_policy_defaults(
    struct input_proxy_session_config *config)
{
    if (config == NULL) {
        return;
    }

    *config = (struct input_proxy_session_config) {
        .activity_timeout_ms = INPUT_PROXY_DEFAULT_ACTIVITY_TIMEOUT_MS,
        .detection_throttle_ms = INPUT_PROXY_DEFAULT_DETECTION_THROTTLE_MS,
        .running_motion_activity = true,
        .paused_motion_activity = true
    };
}

bool input_proxy_runtime_policy_parse_duration(
    const char *text,
    uint64_t *duration_ms)
{
    char *end;
    const unsigned char *character;
    uintmax_t value;

    if (text == NULL || duration_ms == NULL || text[0] == '\0') {
        return false;
    }
    for (character = (const unsigned char *)text; *character != '\0';
         character++) {
        if (!isdigit(*character)) {
            return false;
        }
    }
    errno = 0;
    end = NULL;
    value = strtoumax(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' ||
        value > UINT32_MAX) {
        return false;
    }
    *duration_ms = (uint64_t)value;
    return true;
}

bool input_proxy_runtime_policy_parse_on_off(
    const char *text,
    bool *enabled)
{
    if (text == NULL || enabled == NULL) {
        return false;
    }
    if (strcmp(text, "on") == 0) {
        *enabled = true;
        return true;
    }
    if (strcmp(text, "off") == 0) {
        *enabled = false;
        return true;
    }
    return false;
}

bool input_proxy_runtime_policy_is_valid(
    const struct input_proxy_session_config *config)
{
    return config != NULL && config->source_path != NULL &&
        config->instance_name != NULL &&
        config->activity_timeout_ms <= UINT32_MAX &&
        config->detection_throttle_ms <= UINT32_MAX;
}
