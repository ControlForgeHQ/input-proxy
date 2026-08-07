#include <input_proxy/proxy_session.h>
#include <input_proxy/result.h>
#include <input_proxy/version.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(FILE *stream, const char *program_name)
{
    fprintf(
        stream,
        "Usage:\n"
        "  %s --source PATH --name NAME [--verbose]\n"
        "  %s --help\n"
        "  %s --version\n",
        program_name,
        program_name,
        program_name
    );
}

static enum input_proxy_result parse_config(
    int argc,
    char *argv[],
    struct input_proxy_session_config *config)
{
    int index;

    if (config == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    *config = (struct input_proxy_session_config) {0};

    for (index = 1; index < argc; ++index) {
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

int main(int argc, char *argv[])
{
    struct input_proxy_session_config config;
    struct input_proxy_session *session = NULL;
    enum input_proxy_result result;

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_usage(stdout, argv[0]);
        return EXIT_SUCCESS;
    }

    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("input-proxy %s\n", INPUT_PROXY_VERSION_STRING);
        return EXIT_SUCCESS;
    }

    result = parse_config(argc, argv, &config);
    if (result != INPUT_PROXY_SUCCESS) {
        fprintf(
            stderr,
            "input-proxy: invalid command-line arguments: %s\n",
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

    result = input_proxy_session_run(session);
    if (result != INPUT_PROXY_SUCCESS) {
        fprintf(
            stderr,
            "input-proxy: proxy session failed: %s\n",
            input_proxy_result_string(result)
        );
    }

    input_proxy_session_destroy(session);

    return result == INPUT_PROXY_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
