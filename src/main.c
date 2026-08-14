#define _POSIX_C_SOURCE 200809L

#include <input_proxy/proxy_session.h>
#include <input_proxy/result.h>
#include <input_proxy/version.h>

#include "device_discovery_internal.h"
#include "device_inspection_internal.h"
#include "runtime_discovery_internal.h"

#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct input_proxy_session *active_session;

static void handle_termination_signal(int signal_number)
{
    (void)signal_number;
    input_proxy_session_request_shutdown(active_session);
}

static bool install_signal_handlers(void)
{
    const struct sigaction action = {
        .sa_handler = handle_termination_signal
    };

    if (sigaction(SIGINT, &action, NULL) != 0) {
        return false;
    }

    if (sigaction(SIGTERM, &action, NULL) != 0) {
        const struct sigaction default_action = {
            .sa_handler = SIG_DFL
        };

        (void)sigaction(SIGINT, &default_action, NULL);
        return false;
    }

    return true;
}

static void restore_signal_handlers(void)
{
    const struct sigaction action = {
        .sa_handler = SIG_DFL
    };

    (void)sigaction(SIGINT, &action, NULL);
    (void)sigaction(SIGTERM, &action, NULL);
}

static void print_top_level_help(FILE *stream)
{
    fprintf(
        stream,
        "input-proxy %s\n"
        "Transparent Linux evdev-to-uinput input proxy.\n"
        "\n"
        "Usage:\n"
        "  input-proxy COMMAND [OPTIONS]\n"
        "  input-proxy --help\n"
        "  input-proxy --version\n"
        "\n"
        "Commands:\n"
        "  run      Proxy one physical input device.\n"
        "  list     List available physical input devices concisely.\n"
        "  inspect  Inspect one input device with read-only diagnostics.\n"
        "\n"
        "Global options:\n"
        "  --help     Show this help and exit.\n"
        "  --version  Show the application version and exit.\n"
        "\n"
        "Examples:\n"
        "  input-proxy run --source /dev/input/event0 --name touchscreen\n"
        "  input-proxy list\n"
        "  input-proxy inspect /dev/input/event0\n"
        "\n"
        "Report bugs and find the project at:\n"
        "  https://github.com/fasteddy516/input-proxy\n\n",
        INPUT_PROXY_VERSION_STRING
    );
}

static void print_run_help(FILE *stream)
{
    fputs(
        "Run the input proxy for one physical input device.\n"
        "\n"
        "Usage:\n"
        "  input-proxy run --source PATH --name NAME [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  --source PATH  Physical evdev source device.\n"
        "  --name NAME    Instance Name (also used as the virtual device name).\n"
        "  --activity-timeout-ms MS\n"
        "                 Running activity hold time, 0-4294967295 "
        "(default: 5000).\n"
        "  --detection-throttle-ms MS\n"
        "                 Paused activity throttle, 0-4294967295 "
        "(default: 250).\n"
        "  --running-motion-activity on|off\n"
        "                 Motion counts as activity while running "
        "(default: on).\n"
        "  --paused-motion-activity on|off\n"
        "                 Motion counts as activity while paused "
        "(default: on).\n"
        "  --verbose      Show additional lifecycle diagnostics.\n"
        "  --help         Show this help and exit.\n\n",
        stream
    );
}

static bool parse_on_off(const char *text, bool *enabled)
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

static bool parse_duration_ms(const char *text, uint64_t *duration_ms)
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

static void print_startup_header(
    const struct input_proxy_session_config *config)
{
    printf(
        "input-proxy: Version=%s\n"
        "input-proxy: Repository=https://github.com/fasteddy516/input-proxy\n"
        "input-proxy: Source=%s\n"
        "input-proxy: InstanceName=%s\n"
        "input-proxy: activity timeout=%" PRIu64 "ms%s, motion activity "
        "while running=%s%s\n"
        "input-proxy: detection throttle=%" PRIu64 "ms%s, motion activity "
        "while paused=%s%s\n",
        INPUT_PROXY_VERSION_STRING,
        config->source_path,
        config->instance_name,
        config->activity_timeout_ms,
        config->activity_timeout_ms ==
            INPUT_PROXY_DEFAULT_ACTIVITY_TIMEOUT_MS ? " (default)" : "",
        config->running_motion_activity ? "yes" : "no",
        config->running_motion_activity ? " (default)" : "",
        config->detection_throttle_ms,
        config->detection_throttle_ms ==
            INPUT_PROXY_DEFAULT_DETECTION_THROTTLE_MS ? " (default)" : "",
        config->paused_motion_activity ? "yes" : "no",
        config->paused_motion_activity ? " (default)" : ""
    );
    fflush(stdout);
}

static void print_list_help(FILE *stream)
{
    fputs(
        "List available physical input devices concisely.\n"
        "\n"
        "Usage:\n"
        "  input-proxy list\n"
        "\n"
        "The listing is intended to identify likely proxy sources without an\n"
        "exhaustive capability dump. Virtual uinput devices are excluded when\n"
        "they can be identified reliably. When system D-Bus is available,\n"
        "currently running input-proxy instances are also listed.\n"
        "\n"
        "Options:\n"
        "  --help  Show this help and exit.\n\n",
        stream
    );
}

static void print_inspect_help(FILE *stream)
{
    fputs(
        "Inspect one input device with read-only diagnostics.\n"
        "\n"
        "Usage:\n"
        "  input-proxy inspect PATH\n"
        "\n"
        "Arguments:\n"
        "  PATH  Physical evdev source device to inspect.\n"
        "\n"
        "Inspection reports device identity, capabilities, accessibility, and\n"
        "proxy readiness without modifying the device or system configuration.\n"
        "Associated runtime instances are also reported when system D-Bus is\n"
        "available.\n"
        "\n"
        "Options:\n"
        "  --help  Show this help and exit.\n\n",
        stream
    );
}

static enum input_proxy_result parse_run_config(
    int argc,
    char *argv[],
    struct input_proxy_session_config *config)
{
    int index;
    bool activity_timeout_seen = false;
    bool detection_throttle_seen = false;
    bool running_motion_seen = false;
    bool paused_motion_seen = false;

    if (config == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    *config = (struct input_proxy_session_config) {
        .activity_timeout_ms = INPUT_PROXY_DEFAULT_ACTIVITY_TIMEOUT_MS,
        .detection_throttle_ms = INPUT_PROXY_DEFAULT_DETECTION_THROTTLE_MS,
        .running_motion_activity = true,
        .paused_motion_activity = true
    };

    for (index = 2; index < argc; ++index) {
        if (strcmp(argv[index], "--source") == 0) {
            if (config->source_path != NULL || index + 1 >= argc) {
                return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
            }

            config->source_path = argv[++index];
            continue;
        }

        if (strcmp(argv[index], "--name") == 0) {
            if (config->instance_name != NULL || index + 1 >= argc) {
                return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
            }

            config->instance_name = argv[++index];
            continue;
        }

        if (strcmp(argv[index], "--verbose") == 0) {
            if (config->verbose) {
                return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
            }

            config->verbose = true;
            continue;
        }

        if (strcmp(argv[index], "--activity-timeout-ms") == 0) {
            if (activity_timeout_seen || index + 1 >= argc || !parse_duration_ms(
                    argv[index + 1], &config->activity_timeout_ms)) {
                fprintf(stderr, "input-proxy: invalid non-negative duration "
                    "for --activity-timeout-ms: %s\n",
                    index + 1 < argc ? argv[index + 1] : "missing value");
                return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
            }
            activity_timeout_seen = true;
            index++;
            continue;
        }

        if (strcmp(argv[index], "--detection-throttle-ms") == 0) {
            if (detection_throttle_seen || index + 1 >= argc || !parse_duration_ms(
                    argv[index + 1], &config->detection_throttle_ms)) {
                fprintf(stderr, "input-proxy: invalid non-negative duration "
                    "for --detection-throttle-ms: %s\n",
                    index + 1 < argc ? argv[index + 1] : "missing value");
                return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
            }
            detection_throttle_seen = true;
            index++;
            continue;
        }

        if (strcmp(argv[index], "--running-motion-activity") == 0) {
            if (running_motion_seen || index + 1 >= argc || !parse_on_off(
                    argv[index + 1], &config->running_motion_activity)) {
                fprintf(stderr, "input-proxy: invalid value for "
                    "--running-motion-activity: expected 'on' or 'off'\n");
                return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
            }
            running_motion_seen = true;
            index++;
            continue;
        }

        if (strcmp(argv[index], "--paused-motion-activity") == 0) {
            if (paused_motion_seen || index + 1 >= argc || !parse_on_off(
                    argv[index + 1], &config->paused_motion_activity)) {
                fprintf(stderr, "input-proxy: invalid value for "
                    "--paused-motion-activity: expected 'on' or 'off'\n");
                return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
            }
            paused_motion_seen = true;
            index++;
            continue;
        }

        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    if (config->source_path == NULL || config->instance_name == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    return INPUT_PROXY_SUCCESS;
}

static int run_proxy(int argc, char *argv[])
{
    struct input_proxy_session_config config;
    struct input_proxy_session *session = NULL;
    enum input_proxy_result result;

    result = parse_run_config(argc, argv, &config);
    if (result != INPUT_PROXY_SUCCESS) {
        fprintf(
            stderr,
            "input-proxy: invalid run arguments: %s\n",
            input_proxy_result_string(result)
        );
        print_run_help(stderr);
        return EXIT_FAILURE;
    }

    result = input_proxy_session_create(&session, &config);
    if (result != INPUT_PROXY_SUCCESS) {
        if (result == INPUT_PROXY_ERROR_INSTANCE_NAME_OWNED) {
            fprintf(
                stderr,
                "input-proxy: Instance Name '%s' is already owned by "
                "another running input-proxy instance\n",
                config.instance_name
            );
            fputc('\n', stderr);
            return EXIT_FAILURE;
        }
        if (result == INPUT_PROXY_ERROR_INVALID_INSTANCE_NAME) {
            fprintf(
                stderr,
                "input-proxy: invalid Instance Name '%s': use 1-79 ASCII "
                "bytes, beginning with a letter or underscore, followed "
                "only by letters, digits, underscores, or hyphens\n\n",
                config.instance_name
            );
            return EXIT_FAILURE;
        }
        fprintf(
            stderr,
            "input-proxy: failed to create proxy session: %s\n",
            input_proxy_result_string(result)
        );
        fputc('\n', stderr);
        return EXIT_FAILURE;
    }

    active_session = session;
    if (!install_signal_handlers()) {
        fprintf(stderr, "input-proxy: failed to install signal handlers\n");
        fputc('\n', stderr);
        active_session = NULL;
        input_proxy_session_destroy(session);
        return EXIT_FAILURE;
    }

    print_startup_header(&config);
    result = input_proxy_session_run(session);
    if (result != INPUT_PROXY_SUCCESS) {
        fprintf(
            stderr,
            "input-proxy: proxy session failed: %s\n",
            input_proxy_result_string(result)
        );
    }

    restore_signal_handlers();
    active_session = NULL;
    input_proxy_session_destroy(session);

    fputc('\n', result == INPUT_PROXY_SUCCESS ? stdout : stderr);

    return result == INPUT_PROXY_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int list_devices(int argc)
{
    struct input_proxy_runtime_snapshot snapshot;
    enum input_proxy_result result;

    if (argc != 2) {
        fprintf(stderr, "input-proxy: invalid list arguments\n");
        print_list_help(stderr);
        return EXIT_FAILURE;
    }

    result = input_proxy_list_devices(
        stdout, "/sys/class/input", "/dev/input");
    if (result != INPUT_PROXY_SUCCESS) {
        fprintf(
            stderr,
            "input-proxy: failed to enumerate input devices: %s\n",
            input_proxy_result_string(result)
        );
        fputc('\n', stderr);
        return EXIT_FAILURE;
    }

    input_proxy_runtime_discover(&snapshot);
    input_proxy_runtime_print_list(stdout, &snapshot);
    input_proxy_runtime_snapshot_destroy(&snapshot);

    return EXIT_SUCCESS;
}

static int inspect_device(int argc, char *argv[])
{
    struct input_proxy_runtime_snapshot snapshot;
    enum input_proxy_result result;

    if (argc != 3) {
        fprintf(stderr, "input-proxy: invalid inspect arguments\n");
        print_inspect_help(stderr);
        return EXIT_FAILURE;
    }
    result = input_proxy_inspect_device(stdout, stderr, argv[2],
        "/sys/class/input", "/dev/input", "/dev/uinput", "/run/udev/data");
    if (result == INPUT_PROXY_SUCCESS) {
        input_proxy_runtime_discover(&snapshot);
        input_proxy_runtime_print_inspect(stdout, &snapshot, argv[2]);
        input_proxy_runtime_snapshot_destroy(&snapshot);
    }
    if (result != INPUT_PROXY_SUCCESS) {
        fputc('\n', stderr);
    }
    return result == INPUT_PROXY_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main(int argc, char *argv[])
{
    const char *command;

    if (argc < 2) {
        fprintf(stderr, "input-proxy: missing command\n");
        print_top_level_help(stderr);
        return EXIT_FAILURE;
    }

    command = argv[1];

    if (strcmp(command, "--help") == 0 && argc == 2) {
        print_top_level_help(stdout);
        return EXIT_SUCCESS;
    }

    if (strcmp(command, "--version") == 0 && argc == 2) {
        printf("input-proxy %s\n\n", INPUT_PROXY_VERSION_STRING);
        return EXIT_SUCCESS;
    }

    if (strcmp(command, "run") == 0) {
        if (argc == 3 && strcmp(argv[2], "--help") == 0) {
            print_run_help(stdout);
            return EXIT_SUCCESS;
        }

        return run_proxy(argc, argv);
    }

    if (strcmp(command, "list") == 0) {
        if (argc == 3 && strcmp(argv[2], "--help") == 0) {
            print_list_help(stdout);
            return EXIT_SUCCESS;
        }

        return list_devices(argc);
    }

    if (strcmp(command, "inspect") == 0) {
        if (argc == 3 && strcmp(argv[2], "--help") == 0) {
            print_inspect_help(stdout);
            return EXIT_SUCCESS;
        }

        return inspect_device(argc, argv);
    }

    fprintf(stderr, "input-proxy: unknown command '%s'\n", command);
    print_top_level_help(stderr);
    return EXIT_FAILURE;
}
