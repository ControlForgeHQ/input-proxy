#ifndef INPUT_PROXY_INSTALLED_INSTANCE_INTERNAL_H
#define INPUT_PROXY_INSTALLED_INSTANCE_INTERNAL_H

#include <input_proxy/proxy_session.h>

#include <stdbool.h>
#include <stddef.h>

#define INPUT_PROXY_INSTALLED_INSTANCE_DIRECTORY "/etc/input-proxy/instances"

enum input_proxy_installed_instance_result {
    INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS = 0,
    INPUT_PROXY_INSTALLED_INSTANCE_ALREADY_INSTALLED,
    INPUT_PROXY_INSTALLED_INSTANCE_NOT_FOUND,
    INPUT_PROXY_INSTALLED_INSTANCE_INVALID_NAME,
    INPUT_PROXY_INSTALLED_INSTANCE_DIRECTORY_FAILED,
    INPUT_PROXY_INSTALLED_INSTANCE_CREATE_FAILED,
    INPUT_PROXY_INSTALLED_INSTANCE_ENUMERATION_FAILED,
    INPUT_PROXY_INSTALLED_INSTANCE_REMOVAL_FAILED,
    INPUT_PROXY_INSTALLED_INSTANCE_OUT_OF_MEMORY,
    INPUT_PROXY_INSTALLED_INSTANCE_INVALID_ARGUMENT
};

struct input_proxy_installed_instance_store;

struct input_proxy_installed_instance_list {
    char **names;
    size_t count;
};

enum input_proxy_installed_instance_result
input_proxy_installed_instance_store_create(
    struct input_proxy_installed_instance_store **store
);

/* Internal seam for filesystem-isolated tests. */
enum input_proxy_installed_instance_result
input_proxy_installed_instance_store_create_for_directory(
    struct input_proxy_installed_instance_store **store,
    const char *directory
);

void input_proxy_installed_instance_store_destroy(
    struct input_proxy_installed_instance_store *store
);

enum input_proxy_installed_instance_result
input_proxy_installed_instance_path(
    const struct input_proxy_installed_instance_store *store,
    const char *instance_name,
    char **path
);

enum input_proxy_installed_instance_result
input_proxy_installed_instance_exists(
    const struct input_proxy_installed_instance_store *store,
    const char *instance_name,
    bool *exists
);

enum input_proxy_installed_instance_result
input_proxy_installed_instance_enumerate(
    const struct input_proxy_installed_instance_store *store,
    struct input_proxy_installed_instance_list *list
);

void input_proxy_installed_instance_list_destroy(
    struct input_proxy_installed_instance_list *list
);

enum input_proxy_installed_instance_result
input_proxy_installed_instance_create(
    const struct input_proxy_installed_instance_store *store,
    const struct input_proxy_session_config *config
);

enum input_proxy_installed_instance_result
input_proxy_installed_instance_remove(
    const struct input_proxy_installed_instance_store *store,
    const char *instance_name
);

#endif
