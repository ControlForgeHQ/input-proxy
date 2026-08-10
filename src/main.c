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

static void print_usage(FILE *stream, const char *program_name)
{
    fprintf(
        stream,
        "Usage:\n"
        "  %s run --source PATH --name NAME [--verbose]\n"
        "  %s list\n"
        "  %s inspect PATH\n"
        "  %s --help\n"
        "  %s --version\n",
        program_name,
        program_name,
        program_name,
        program_name,
        program_name
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
        print_usage(stderr, argv[0]);
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
        print_usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }

    command = argv[1];

    if (strcmp(command, "--help") == 0 && argc == 2) {
        print_usage(stdout, argv[0]);
        return EXIT_SUCCESS;
    }

    if (strcmp(command, "--version") == 0 && argc == 2) {
        printf("input-proxy %s\n", INPUT_PROXY_VERSION_STRING);
        return EXIT_SUCCESS;
    }

    if (strcmp(command, "run") == 0) {
        return run_proxy(argc, argv);
    }

    if (strcmp(command, "list") == 0 || strcmp(command, "inspect") == 0) {
        fprintf(
            stderr,
            "input-proxy: command '%s' is not yet implemented\n",
            command
        );
        return EXIT_FAILURE;
    }

    fprintf(stderr, "input-proxy: unknown command '%s'\n", command);
    print_usage(stderr, argv[0]);
    return EXIT_FAILURE;
}
