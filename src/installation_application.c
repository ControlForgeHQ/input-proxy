#define _POSIX_C_SOURCE 200809L

#include "installation_application_internal.h"

#include "device_inspection_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define RULE_PREFIX "90-input-proxy-"
#define RULE_SUFFIX ".rules"
#define RULE_TEMPORARY_TEMPLATE "/.input-proxy-rule-XXXXXX"

static bool write_all(int descriptor, const char *content, size_t size)
{
    size_t written = 0;
    while (written < size) {
        ssize_t amount = write(descriptor, content + written, size - written);
        if (amount < 0 && errno == EINTR) continue;
        if (amount <= 0) return false;
        written += (size_t)amount;
    }
    return true;
}

static char *rule_path(const char *directory, const char *name)
{
    size_t size = strlen(directory) + strlen(name) +
        sizeof(RULE_PREFIX) + sizeof(RULE_SUFFIX);
    char *path = malloc(size);
    if (path != NULL)
        (void)snprintf(path, size, "%s/%s%s%s", directory, RULE_PREFIX,
            name, RULE_SUFFIX);
    return path;
}

static char *render_rule(
    const struct input_proxy_device_rule_identity *identity)
{
    char identity_match[512];
    int source_size;
    char *content;

    if (!input_proxy_rule_identity_is_narrow(identity)) return NULL;
    if (input_proxy_rule_identity_has_udev_identity(identity))
        (void)snprintf(identity_match, sizeof(identity_match),
            "ENV{ID_VENDOR_ID}==\"%s\", ENV{ID_MODEL_ID}==\"%s\"",
            identity->udev_vendor, identity->udev_model);
    else
        (void)snprintf(identity_match, sizeof(identity_match),
            "ATTRS{id/bustype}==\"%s\", ATTRS{id/vendor}==\"%s\", ATTRS{id/product}==\"%s\"",
            identity->bus, identity->vendor, identity->product);
    source_size = snprintf(NULL, 0,
        "ACTION==\"add|change\", SUBSYSTEM==\"input\", KERNEL==\"event*\", "
        "%s, ENV{ID_PATH}==\"%s\", ENV{LIBINPUT_IGNORE_DEVICE}=\"1\"\n",
        identity_match, identity->path);
    if (source_size < 0) return NULL;
    content = malloc((size_t)source_size + 1);
    if (content == NULL) return NULL;
    (void)snprintf(content, (size_t)source_size + 1,
        "ACTION==\"add|change\", SUBSYSTEM==\"input\", KERNEL==\"event*\", "
        "%s, ENV{ID_PATH}==\"%s\", ENV{LIBINPUT_IGNORE_DEVICE}=\"1\"\n",
        identity_match, identity->path);
    return content;
}

static bool publish_rule(const char *directory, const char *final_path,
    const char *content, bool inject_failure)
{
    size_t size = strlen(directory) + sizeof(RULE_TEMPORARY_TEMPLATE);
    char *temporary = malloc(size);
    int descriptor = -1;
    bool success = false;
    if (temporary == NULL) return false;
    (void)snprintf(temporary, size, "%s%s", directory, RULE_TEMPORARY_TEMPLATE);
    descriptor = mkstemp(temporary);
    if (descriptor < 0) goto cleanup;
    if (fchmod(descriptor, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) != 0 ||
        !write_all(descriptor, content, strlen(content)) || fsync(descriptor) != 0)
        goto cleanup;
    if (close(descriptor) != 0) { descriptor = -1; goto cleanup; }
    descriptor = -1;
    if (inject_failure || link(temporary, final_path) != 0) goto cleanup;
    success = true;
cleanup:
    if (descriptor >= 0) close(descriptor);
    unlink(temporary);
    free(temporary);
    return success;
}

enum input_proxy_installation_application_result
input_proxy_installation_plan_apply(const struct input_proxy_installation_plan *plan,
    const struct input_proxy_installed_instance_store *store,
    const struct input_proxy_installation_application_environment *environment,
    struct input_proxy_installation_application_failure *failure)
{
    const struct input_proxy_deployment_resolution *resolution;
    const struct input_proxy_deployment_readiness *readiness;
    const struct input_proxy_session_config *config;
    const char *directory = environment != NULL && environment->udev_rule_directory != NULL
        ? environment->udev_rule_directory : INPUT_PROXY_UDEV_RULE_DIRECTORY;
    char *final_rule = NULL;
    char *response_path = NULL;
    char *content = NULL;
    enum input_proxy_installed_instance_result store_result;
    enum input_proxy_installation_application_result result;

    if (failure != NULL) memset(failure, 0, sizeof(*failure));
    resolution = input_proxy_installation_plan_resolution(plan);
    readiness = input_proxy_installation_plan_readiness(plan);
    config = input_proxy_installation_plan_config(plan);
    if (resolution == NULL || readiness == NULL || config == NULL || store == NULL ||
        !resolution->choices_resolved || !resolution->application_ready)
        return INPUT_PROXY_INSTALLATION_APPLICATION_INVALID_PLAN;
    if (!resolution->libinput_ignore_action)
        return input_proxy_installed_instance_create(store, config) ==
            INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS
            ? INPUT_PROXY_INSTALLATION_APPLICATION_SUCCESS
            : INPUT_PROXY_INSTALLATION_APPLICATION_RESPONSE_FAILED;
    content = render_rule(&readiness->rule_identity);
    if (content == NULL)
        return INPUT_PROXY_INSTALLATION_APPLICATION_RULE_GENERATION_FAILED;
    final_rule = rule_path(directory, config->instance_name);
    if (final_rule == NULL) { free(content); return INPUT_PROXY_INSTALLATION_APPLICATION_RULE_GENERATION_FAILED; }
    if (lstat(final_rule, &(struct stat){0}) == 0 || errno != ENOENT) {
        free(final_rule); free(content);
        return INPUT_PROXY_INSTALLATION_APPLICATION_RULE_PUBLICATION_FAILED;
    }
    store_result = input_proxy_installed_instance_create(store, config);
    if (store_result != INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS) {
        free(final_rule); free(content);
        return INPUT_PROXY_INSTALLATION_APPLICATION_RESPONSE_FAILED;
    }
    if (!publish_rule(directory, final_rule, content,
            environment != NULL && environment->inject_rule_publication_failure)) {
        result = INPUT_PROXY_INSTALLATION_APPLICATION_RULE_PUBLICATION_FAILED;
        if ((environment != NULL && environment->inject_response_rollback_failure) ||
            input_proxy_installed_instance_remove(store, config->instance_name) !=
                INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS) {
            result = INPUT_PROXY_INSTALLATION_APPLICATION_ROLLBACK_FAILED;
            if (failure != NULL && input_proxy_installed_instance_path(store,
                    config->instance_name, &response_path) == INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS) {
                (void)snprintf(failure->rollback_path, sizeof(failure->rollback_path), "%s", response_path);
                free(response_path);
            }
        }
        free(final_rule); free(content);
        if (failure != NULL) failure->result = result;
        return result;
    }
    free(final_rule); free(content);
    return INPUT_PROXY_INSTALLATION_APPLICATION_SUCCESS;
}
