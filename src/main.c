#define _POSIX_C_SOURCE 200809L

#include <input_proxy/proxy_session.h>
#include <input_proxy/result.h>
#include <input_proxy/version.h>

#include <stdbool.h>
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
        "  https://github.com/fasteddy516/input-proxy\n",
        INPUT_PROXY_VERSION_STRING
    );
}

static void print_run_help(FILE *stream)
{
    fputs(
        "Run the input proxy for one physical input device.\n"
        "\n"
        "Usage:\n"
        "  input-proxy run --source PATH --name NAME [--verbose]\n"
        "\n"
        "Options:\n"
        "  --source PATH  Physical evdev source device.\n"
        "  --name NAME    Name of the virtual input device.\n"
        "  --verbose      Show additional lifecycle diagnostics.\n"
        "  --help         Show this help and exit.\n",
        stream
    );
}

static void print_startup_header(
    const struct input_proxy_session_config *config)
{
    printf(
        "input-proxy: Version=%s\n"
        "input-proxy: Repository=https://github.com/fasteddy516/input-proxy\n"
        "input-proxy: Source=%s\n"
        "input-proxy: DeviceName=%s\n",
        INPUT_PROXY_VERSION_STRING,
        config->source_path,
        config->device_name
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
        "they can be identified reliably.\n"
        "\n"
        "Options:\n"
        "  --help  Show this help and exit.\n",
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
        "\n"
        "Options:\n"
        "  --help  Show this help and exit.\n",
        stream
    );
}

static enum input_proxy_result parse_run_config(
    int argc,
    char *argv[],
    struct input_proxy_session_config *config)
{
    int index;

    if (config == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    *config = (struct input_proxy_session_config) {0};

    for (index = 2; index < argc; ++index) {
        if (strcmp(argv[index], "--source") == 0) {
            if (config->source_path != NULL || index + 1 >= argc) {
                return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
            }

            config->source_path = argv[++index];
            continue;
        }

        if (strcmp(argv[index], "--name") == 0) {
            if (config->device_name != NULL || index + 1 >= argc) {
                return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
            }

            config->device_name = argv[++index];
            continue;
        }

        if (strcmp(argv[index], "--verbose") == 0) {
            if (config->verbose) {
                return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
            }

            config->verbose = true;
            continue;
        }

        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    if (config->source_path == NULL || config->device_name == NULL) {
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
        fprintf(
            stderr,
            "input-proxy: failed to create proxy session: %s\n",
            input_proxy_result_string(result)
        );
        return EXIT_FAILURE;
    }

    active_session = session;
    if (!install_signal_handlers()) {
        fprintf(stderr, "input-proxy: failed to install signal handlers\n");
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
        printf("input-proxy %s\n", INPUT_PROXY_VERSION_STRING);
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

        fprintf(stderr, "input-proxy: command 'list' is not yet implemented\n");
        return EXIT_FAILURE;
    }

    if (strcmp(command, "inspect") == 0) {
        if (argc == 3 && strcmp(argv[2], "--help") == 0) {
            print_inspect_help(stdout);
            return EXIT_SUCCESS;
        }

        fprintf(
            stderr,
            "input-proxy: command '%s' is not yet implemented\n",
            command
        );
        return EXIT_FAILURE;
    }

    fprintf(stderr, "input-proxy: unknown command '%s'\n", command);
    print_top_level_help(stderr);
    return EXIT_FAILURE;
}
