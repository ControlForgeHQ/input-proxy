#ifndef INPUT_PROXY_RESPONSE_FILE_INTERNAL_H
#define INPUT_PROXY_RESPONSE_FILE_INTERNAL_H

#include <stdbool.h>

bool input_proxy_response_file_read(
    const char *path,
    char *program_name,
    char ***response_argv,
    int *response_argc
);

void input_proxy_response_file_free(int argc, char **argv);

#endif
