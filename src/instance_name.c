#define _GNU_SOURCE

#include "instance_name_internal.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define INSTANCE_SOCKET_PREFIX "input-proxy:"

struct input_proxy_instance_name {
    int socket_fd;
};

static int is_ascii_letter(unsigned char byte)
{
    return (byte >= 'A' && byte <= 'Z') ||
        (byte >= 'a' && byte <= 'z');
}

enum input_proxy_result input_proxy_instance_name_validate(const char *name)
{
    size_t index;
    size_t length;

    if (name == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    length = strlen(name);
    if (length == 0 || length > 79) {
        return INPUT_PROXY_ERROR_INVALID_INSTANCE_NAME;
    }

    if (!is_ascii_letter((unsigned char)name[0]) && name[0] != '_') {
        return INPUT_PROXY_ERROR_INVALID_INSTANCE_NAME;
    }

    for (index = 1; index < length; ++index) {
        const unsigned char byte = (unsigned char)name[index];

        if (!is_ascii_letter(byte) &&
            !(byte >= '0' && byte <= '9') &&
            byte != '_' && byte != '-') {
            return INPUT_PROXY_ERROR_INVALID_INSTANCE_NAME;
        }
    }

    return INPUT_PROXY_SUCCESS;
}

enum input_proxy_result input_proxy_instance_name_acquire(
    struct input_proxy_instance_name **ownership,
    const char *name)
{
    struct input_proxy_instance_name *new_ownership;
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    const size_t prefix_length = sizeof(INSTANCE_SOCKET_PREFIX) - 1;
    size_t name_length;
    socklen_t address_length;

    if (ownership == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    *ownership = NULL;
    {
        enum input_proxy_result result =
            input_proxy_instance_name_validate(name);

        if (result != INPUT_PROXY_SUCCESS) {
            return result;
        }
    }

    name_length = strlen(name);
    if (prefix_length + name_length > sizeof(address.sun_path) - 1) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    new_ownership = calloc(1, sizeof(*new_ownership));
    if (new_ownership == NULL) {
        return INPUT_PROXY_ERROR_OUT_OF_MEMORY;
    }

    new_ownership->socket_fd = socket(
        AF_UNIX,
        SOCK_DGRAM | SOCK_CLOEXEC,
        0
    );
    if (new_ownership->socket_fd < 0) {
        free(new_ownership);
        return INPUT_PROXY_ERROR_INSTANCE_NAME_OWNERSHIP_FAILED;
    }

    address.sun_path[0] = '\0';
    memcpy(address.sun_path + 1, INSTANCE_SOCKET_PREFIX, prefix_length);
    memcpy(address.sun_path + 1 + prefix_length, name, name_length);
    address_length = (socklen_t)(
        offsetof(struct sockaddr_un, sun_path) + 1 + prefix_length + name_length
    );

    if (bind(
            new_ownership->socket_fd,
            (const struct sockaddr *)&address,
            address_length
        ) != 0) {
        enum input_proxy_result result =
            errno == EADDRINUSE
                ? INPUT_PROXY_ERROR_INSTANCE_NAME_OWNED
                : INPUT_PROXY_ERROR_INSTANCE_NAME_OWNERSHIP_FAILED;

        close(new_ownership->socket_fd);
        free(new_ownership);
        return result;
    }

    *ownership = new_ownership;
    return INPUT_PROXY_SUCCESS;
}

void input_proxy_instance_name_release(
    struct input_proxy_instance_name *ownership)
{
    if (ownership == NULL) {
        return;
    }

    close(ownership->socket_fd);
    free(ownership);
}
