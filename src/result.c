#include <input_proxy/result.h>

const char *input_proxy_result_string(enum input_proxy_result result)
{
    switch (result) {
        case INPUT_PROXY_SUCCESS:
            return "Success";

        case INPUT_PROXY_EVENT_SYNC_REQUIRED:
            return "Input event synchronization required";

        case INPUT_PROXY_EVENT_UNAVAILABLE:
            return "Input event temporarily unavailable";

        case INPUT_PROXY_ERROR_UNKNOWN:
            return "Unknown error";

        case INPUT_PROXY_ERROR_NOT_IMPLEMENTED:
            return "Not implemented";

        case INPUT_PROXY_ERROR_INVALID_ARGUMENT:
            return "Invalid argument";

        case INPUT_PROXY_ERROR_OUT_OF_MEMORY:
            return "Out of memory";

        case INPUT_PROXY_ERROR_SOURCE_UNAVAILABLE:
            return "Source device unavailable";

        case INPUT_PROXY_ERROR_SOURCE_OPEN_FAILED:
            return "Failed to open source device";

        case INPUT_PROXY_ERROR_SOURCE_INCOMPATIBLE:
            return "Source device is incompatible";

        case INPUT_PROXY_ERROR_SOURCE_DISCONNECTED:
            return "Source device disconnected";

        case INPUT_PROXY_ERROR_UINPUT_UNAVAILABLE:
            return "uinput is unavailable";

        case INPUT_PROXY_ERROR_VIRTUAL_DEVICE_CREATE_FAILED:
            return "Failed to create virtual device";

        case INPUT_PROXY_ERROR_EVENT_READ_FAILED:
            return "Failed to read input event";

        case INPUT_PROXY_ERROR_EVENT_WRITE_FAILED:
            return "Failed to write input event";

        case INPUT_PROXY_ERROR_INTERNAL:
            return "Internal error";
    }

    return "Unrecognized result code";
}
