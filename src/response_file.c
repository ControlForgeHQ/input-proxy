#define _POSIX_C_SOURCE 200809L

#include "response_file_internal.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void input_proxy_response_file_free(int argc, char **argv)
{
    int index;

    if (argv == NULL) return;
    for (index = 2; index < argc; ++index) free(argv[index]);
    free(argv);
}

static bool append_argument(char ***argv, int *argc,
    const char *line, size_t length)
{
    char **resized;
    char *argument;

    if (*argc == INT_MAX) return false;
    argument = malloc(length + 1);
    if (argument == NULL) return false;
    memcpy(argument, line, length);
    argument[length] = '\0';
    resized = realloc(*argv, ((size_t)*argc + 1) * sizeof(**argv));
    if (resized == NULL) { free(argument); return false; }
    *argv = resized;
    (*argv)[(*argc)++] = argument;
    return true;
}

bool input_proxy_response_file_read(const char *path, char *program_name,
    char ***response_argv, int *response_argc)
{
    FILE *file;
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    bool success = false;
    char **argv = NULL;
    int argc = 2;

    if (path == NULL || program_name == NULL || response_argv == NULL ||
        response_argc == NULL) return false;
    if (path[0] == '\0') {
        fprintf(stderr, "input-proxy: response-file path after '@' is empty\n");
        return false;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "input-proxy: cannot open response file '%s': %s\n",
            path, strerror(errno));
        return false;
    }
    argv = malloc(2 * sizeof(*argv));
    if (argv == NULL) {
        fprintf(stderr, "input-proxy: cannot allocate response-file arguments\n");
        goto cleanup;
    }
    argv[0] = program_name;
    argv[1] = "run";
    errno = 0;
    while ((length = getline(&line, &capacity, file)) >= 0) {
        size_t argument_length = (size_t)length;
        if (memchr(line, '\0', argument_length) != NULL) {
            fprintf(stderr, "input-proxy: response file '%s' contains an embedded NUL byte\n", path);
            goto cleanup;
        }
        if (argument_length > 0 && line[argument_length - 1] == '\n')
            argument_length--;
        if (!append_argument(&argv, &argc, line, argument_length)) {
            fprintf(stderr, "input-proxy: cannot allocate response-file arguments\n");
            goto cleanup;
        }
        errno = 0;
    }
    if (ferror(file)) {
        fprintf(stderr, "input-proxy: cannot read response file '%s': %s\n",
            path, errno != 0 ? strerror(errno) : "input/output error");
        goto cleanup;
    }
    if (fclose(file) != 0) {
        file = NULL;
        fprintf(stderr, "input-proxy: cannot finish reading response file '%s': %s\n",
            path, strerror(errno));
        goto cleanup;
    }
    file = NULL;
    *response_argv = argv;
    *response_argc = argc;
    argv = NULL;
    success = true;

cleanup:
    if (file != NULL) (void)fclose(file);
    free(line);
    input_proxy_response_file_free(argc, argv);
    return success;
}
