#define _POSIX_C_SOURCE 200809L

#include "installed_instance_internal.h"

#include "instance_name_internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define ARTIFACT_SUFFIX ".args"
#define TEMPORARY_TEMPLATE "/.input-proxy-artifact-XXXXXX"

struct input_proxy_installed_instance_store {
    char *directory;
};

static enum input_proxy_installed_instance_result validate_name(
    const char *instance_name)
{
    enum input_proxy_result result =
        input_proxy_instance_name_validate(instance_name);

    if (result == INPUT_PROXY_SUCCESS) {
        return INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS;
    }
    if (result == INPUT_PROXY_ERROR_OUT_OF_MEMORY) {
        return INPUT_PROXY_INSTALLED_INSTANCE_OUT_OF_MEMORY;
    }
    if (result == INPUT_PROXY_ERROR_INVALID_ARGUMENT) {
        return INPUT_PROXY_INSTALLED_INSTANCE_INVALID_ARGUMENT;
    }
    return INPUT_PROXY_INSTALLED_INSTANCE_INVALID_NAME;
}

enum input_proxy_installed_instance_result
input_proxy_installed_instance_store_create_for_directory(
    struct input_proxy_installed_instance_store **store,
    const char *directory)
{
    struct input_proxy_installed_instance_store *new_store;

    if (store == NULL || directory == NULL || directory[0] == '\0') {
        return INPUT_PROXY_INSTALLED_INSTANCE_INVALID_ARGUMENT;
    }
    *store = NULL;

    new_store = calloc(1, sizeof(*new_store));
    if (new_store == NULL) {
        return INPUT_PROXY_INSTALLED_INSTANCE_OUT_OF_MEMORY;
    }
    new_store->directory = strdup(directory);
    if (new_store->directory == NULL) {
        free(new_store);
        return INPUT_PROXY_INSTALLED_INSTANCE_OUT_OF_MEMORY;
    }

    *store = new_store;
    return INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS;
}

enum input_proxy_installed_instance_result
input_proxy_installed_instance_store_create(
    struct input_proxy_installed_instance_store **store)
{
    return input_proxy_installed_instance_store_create_for_directory(
        store,
        INPUT_PROXY_INSTALLED_INSTANCE_DIRECTORY
    );
}

void input_proxy_installed_instance_store_destroy(
    struct input_proxy_installed_instance_store *store)
{
    if (store == NULL) {
        return;
    }
    free(store->directory);
    free(store);
}

enum input_proxy_installed_instance_result
input_proxy_installed_instance_path(
    const struct input_proxy_installed_instance_store *store,
    const char *instance_name,
    char **path)
{
    enum input_proxy_installed_instance_result result;
    size_t path_size;

    if (store == NULL || path == NULL) {
        return INPUT_PROXY_INSTALLED_INSTANCE_INVALID_ARGUMENT;
    }
    *path = NULL;
    result = validate_name(instance_name);
    if (result != INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS) {
        return result;
    }

    path_size = strlen(store->directory) + 1 + strlen(instance_name) +
        sizeof(ARTIFACT_SUFFIX);
    *path = malloc(path_size);
    if (*path == NULL) {
        return INPUT_PROXY_INSTALLED_INSTANCE_OUT_OF_MEMORY;
    }
    if (snprintf(
            *path,
            path_size,
            "%s/%s%s",
            store->directory,
            instance_name,
            ARTIFACT_SUFFIX
        ) < 0) {
        free(*path);
        *path = NULL;
        return INPUT_PROXY_INSTALLED_INSTANCE_CREATE_FAILED;
    }
    return INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS;
}

enum input_proxy_installed_instance_result
input_proxy_installed_instance_exists(
    const struct input_proxy_installed_instance_store *store,
    const char *instance_name,
    bool *exists)
{
    enum input_proxy_installed_instance_result result;
    struct stat status;
    char *path;

    if (exists == NULL) {
        return INPUT_PROXY_INSTALLED_INSTANCE_INVALID_ARGUMENT;
    }
    *exists = false;
    result = input_proxy_installed_instance_path(store, instance_name, &path);
    if (result != INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS) {
        return result;
    }

    if (lstat(path, &status) == 0) {
        *exists = true;
        free(path);
        return INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS;
    }
    free(path);
    if (errno == ENOENT) {
        return INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS;
    }
    return INPUT_PROXY_INSTALLED_INSTANCE_DIRECTORY_FAILED;
}

static int compare_names(const void *left, const void *right)
{
    const char *const *left_name = left;
    const char *const *right_name = right;

    return strcmp(*left_name, *right_name);
}

void input_proxy_installed_instance_list_destroy(
    struct input_proxy_installed_instance_list *list)
{
    size_t index;

    if (list == NULL) {
        return;
    }
    for (index = 0; index < list->count; ++index) {
        free(list->names[index]);
    }
    free(list->names);
    *list = (struct input_proxy_installed_instance_list) { 0 };
}

enum input_proxy_installed_instance_result
input_proxy_installed_instance_enumerate(
    const struct input_proxy_installed_instance_store *store,
    struct input_proxy_installed_instance_list *list)
{
    DIR *directory;
    struct dirent *entry;
    int directory_fd;

    if (store == NULL || list == NULL) {
        return INPUT_PROXY_INSTALLED_INSTANCE_INVALID_ARGUMENT;
    }
    *list = (struct input_proxy_installed_instance_list) { 0 };
    directory = opendir(store->directory);
    if (directory == NULL) {
        return INPUT_PROXY_INSTALLED_INSTANCE_DIRECTORY_FAILED;
    }
    directory_fd = dirfd(directory);

    errno = 0;
    while ((entry = readdir(directory)) != NULL) {
        struct stat status;
        char *name;
        char **resized_names;
        size_t filename_length = strlen(entry->d_name);
        size_t suffix_length = sizeof(ARTIFACT_SUFFIX) - 1;
        size_t name_length;

        if (filename_length <= suffix_length ||
            strcmp(entry->d_name + filename_length - suffix_length,
                ARTIFACT_SUFFIX) != 0) {
            continue;
        }
        name_length = filename_length - suffix_length;
        name = strndup(entry->d_name, name_length);
        if (name == NULL) {
            input_proxy_installed_instance_list_destroy(list);
            closedir(directory);
            return INPUT_PROXY_INSTALLED_INSTANCE_OUT_OF_MEMORY;
        }
        if (validate_name(name) != INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS) {
            free(name);
            continue;
        }
        if (fstatat(
                directory_fd,
                entry->d_name,
                &status,
                AT_SYMLINK_NOFOLLOW
            ) != 0) {
            free(name);
            input_proxy_installed_instance_list_destroy(list);
            closedir(directory);
            return INPUT_PROXY_INSTALLED_INSTANCE_ENUMERATION_FAILED;
        }
        if (!S_ISREG(status.st_mode)) {
            free(name);
            continue;
        }
        resized_names = realloc(
            list->names,
            (list->count + 1) * sizeof(*list->names)
        );
        if (resized_names == NULL) {
            free(name);
            input_proxy_installed_instance_list_destroy(list);
            closedir(directory);
            return INPUT_PROXY_INSTALLED_INSTANCE_OUT_OF_MEMORY;
        }
        list->names = resized_names;
        list->names[list->count++] = name;
        errno = 0;
    }
    if (errno != 0) {
        input_proxy_installed_instance_list_destroy(list);
        closedir(directory);
        return INPUT_PROXY_INSTALLED_INSTANCE_ENUMERATION_FAILED;
    }
    if (closedir(directory) != 0) {
        input_proxy_installed_instance_list_destroy(list);
        return INPUT_PROXY_INSTALLED_INSTANCE_ENUMERATION_FAILED;
    }
    if (list->count > 1) {
        qsort(list->names, list->count, sizeof(*list->names), compare_names);
    }
    return INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS;
}

static enum input_proxy_installed_instance_result write_all(
    int descriptor,
    const char *content,
    size_t content_size)
{
    size_t written = 0;

    while (written < content_size) {
        ssize_t result = write(
            descriptor,
            content + written,
            content_size - written
        );
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return INPUT_PROXY_INSTALLED_INSTANCE_CREATE_FAILED;
        }
        written += (size_t)result;
    }
    return INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS;
}

enum input_proxy_installed_instance_result
input_proxy_installed_instance_create(
    const struct input_proxy_installed_instance_store *store,
    const struct input_proxy_session_config *config)
{
    enum input_proxy_installed_instance_result result;
    char *final_path = NULL;
    char *temporary_path = NULL;
    char *content = NULL;
    size_t temporary_size;
    int descriptor = -1;
    int content_size;

    if (store == NULL || config == NULL || config->source_path == NULL ||
        strchr(config->source_path, '\n') != NULL) {
        return INPUT_PROXY_INSTALLED_INSTANCE_INVALID_ARGUMENT;
    }
    result = input_proxy_installed_instance_path(
        store,
        config->instance_name,
        &final_path
    );
    if (result != INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS) {
        return result;
    }

    content_size = snprintf(
        NULL,
        0,
        "--source\n%s\n--name\n%s\n"
        "--activity-timeout-ms\n%" PRIu64 "\n"
        "--detection-throttle-ms\n%" PRIu64 "\n"
        "--running-motion-activity\n%s\n"
        "--paused-motion-activity\n%s\n"
        "--start-paused\n%s\n",
        config->source_path,
        config->instance_name,
        config->activity_timeout_ms,
        config->detection_throttle_ms,
        config->running_motion_activity ? "on" : "off",
        config->paused_motion_activity ? "on" : "off",
        config->start_paused ? "on" : "off"
    );
    if (content_size < 0) {
        result = INPUT_PROXY_INSTALLED_INSTANCE_CREATE_FAILED;
        goto cleanup;
    }
    content = malloc((size_t)content_size + 1);
    if (content == NULL) {
        result = INPUT_PROXY_INSTALLED_INSTANCE_OUT_OF_MEMORY;
        goto cleanup;
    }
    (void)snprintf(
        content,
        (size_t)content_size + 1,
        "--source\n%s\n--name\n%s\n"
        "--activity-timeout-ms\n%" PRIu64 "\n"
        "--detection-throttle-ms\n%" PRIu64 "\n"
        "--running-motion-activity\n%s\n"
        "--paused-motion-activity\n%s\n"
        "--start-paused\n%s\n",
        config->source_path,
        config->instance_name,
        config->activity_timeout_ms,
        config->detection_throttle_ms,
        config->running_motion_activity ? "on" : "off",
        config->paused_motion_activity ? "on" : "off",
        config->start_paused ? "on" : "off"
    );

    temporary_size = strlen(store->directory) + sizeof(TEMPORARY_TEMPLATE);
    temporary_path = malloc(temporary_size);
    if (temporary_path == NULL) {
        result = INPUT_PROXY_INSTALLED_INSTANCE_OUT_OF_MEMORY;
        goto cleanup;
    }
    (void)snprintf(
        temporary_path,
        temporary_size,
        "%s%s",
        store->directory,
        TEMPORARY_TEMPLATE
    );
    descriptor = mkstemp(temporary_path);
    if (descriptor < 0) {
        result = errno == ENOENT || errno == EACCES
            ? INPUT_PROXY_INSTALLED_INSTANCE_DIRECTORY_FAILED
            : INPUT_PROXY_INSTALLED_INSTANCE_CREATE_FAILED;
        goto cleanup;
    }
    if (fchmod(descriptor, S_IRUSR | S_IWUSR | S_IRGRP) != 0) {
        result = INPUT_PROXY_INSTALLED_INSTANCE_CREATE_FAILED;
        goto cleanup;
    }
    result = write_all(descriptor, content, (size_t)content_size);
    if (result != INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS ||
        fsync(descriptor) != 0) {
        result = INPUT_PROXY_INSTALLED_INSTANCE_CREATE_FAILED;
        goto cleanup;
    }
    if (close(descriptor) != 0) {
        descriptor = -1;
        result = INPUT_PROXY_INSTALLED_INSTANCE_CREATE_FAILED;
        goto cleanup;
    }
    descriptor = -1;

    if (link(temporary_path, final_path) != 0) {
        result = errno == EEXIST
            ? INPUT_PROXY_INSTALLED_INSTANCE_ALREADY_INSTALLED
            : INPUT_PROXY_INSTALLED_INSTANCE_CREATE_FAILED;
        goto cleanup;
    }
    if (unlink(temporary_path) == 0) {
        temporary_path[0] = '\0';
    }
    result = INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS;

cleanup:
    if (descriptor >= 0) {
        close(descriptor);
    }
    if (temporary_path != NULL && temporary_path[0] != '\0') {
        unlink(temporary_path);
    }
    free(content);
    free(temporary_path);
    free(final_path);
    return result;
}

enum input_proxy_installed_instance_result
input_proxy_installed_instance_remove(
    const struct input_proxy_installed_instance_store *store,
    const char *instance_name)
{
    enum input_proxy_installed_instance_result result;
    char *path;

    result = input_proxy_installed_instance_path(store, instance_name, &path);
    if (result != INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS) {
        return result;
    }
    if (unlink(path) != 0) {
        result = errno == ENOENT
            ? INPUT_PROXY_INSTALLED_INSTANCE_NOT_FOUND
            : INPUT_PROXY_INSTALLED_INSTANCE_REMOVAL_FAILED;
    }
    free(path);
    return result;
}
