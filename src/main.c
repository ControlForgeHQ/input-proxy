#include <stdio.h>
#include <string.h>

#include <input_proxy/version.h>

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

int main(int argc, char *argv[])
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_usage(stdout, argv[0]);
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("input-proxy %s\n", INPUT_PROXY_VERSION_STRING);
        return 0;
    }

    print_usage(stderr, argv[0]);
    return 1;
}
