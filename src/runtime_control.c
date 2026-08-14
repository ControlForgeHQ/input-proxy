#define _POSIX_C_SOURCE 200809L

#include "runtime_control_internal.h"

#include <input_proxy/version.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>
#include <time.h>
#include <unistd.h>

#define SERVICE_PREFIX "net.controlforge.InputProxy1.Instance."
#define OBJECT_PATH "/net/controlforge/InputProxy1/Instance"
#define INTERFACE_NAME "net.controlforge.InputProxy1.Instance"

struct input_proxy_runtime_control {
    sd_bus *bus;
    sd_bus_slot *object_slot;
    const struct input_proxy_runtime_control_state *state;
    char service_name[sizeof(SERVICE_PREFIX) + 79];
};

static int property_string(sd_bus *bus, const char *path,
    const char *interface, const char *property, sd_bus_message *reply,
    void *userdata, sd_bus_error *error)
{
    const struct input_proxy_runtime_control_state *state = userdata;
    const char *value;

    (void)bus; (void)path; (void)interface; (void)error;
    value = strcmp(property, "InstanceName") == 0 ? state->instance_name :
        strcmp(property, "Source") == 0 ? state->source_path :
        INPUT_PROXY_VERSION_STRING;
    return sd_bus_message_append(reply, "s", value);
}

static int property_pid(sd_bus *bus, const char *path, const char *interface,
    const char *property, sd_bus_message *reply, void *userdata,
    sd_bus_error *error)
{
    (void)bus; (void)path; (void)interface; (void)property;
    (void)userdata; (void)error;
    return sd_bus_message_append(reply, "u", (uint32_t)getpid());
}

static int property_boolean(sd_bus *bus, const char *path,
    const char *interface, const char *property, sd_bus_message *reply,
    void *userdata, sd_bus_error *error)
{
    const struct input_proxy_runtime_control_state *state = userdata;
    bool value;

    (void)bus; (void)path; (void)interface; (void)error;
    if (strcmp(property, "Paused") == 0) {
        value = state->paused;
    } else if (strcmp(property, "SourceAvailable") == 0) {
        value = state->source_available;
    } else if (strcmp(property, "ActivityWhileRunning") == 0) {
        value = state->activity_while_running;
    } else {
        value = state->activity_while_paused;
    }
    return sd_bus_message_append(reply, "b", value);
}

static int unsupported_method(sd_bus_message *message, void *userdata,
    sd_bus_error *error)
{
    (void)message; (void)userdata;
    return sd_bus_error_set_const(error, SD_BUS_ERROR_NOT_SUPPORTED,
        "Runtime pause and resume control is not implemented");
}

static const sd_bus_vtable runtime_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("InstanceName", "s", property_string, 0,
        SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Source", "s", property_string, 0,
        SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Version", "s", property_string, 0,
        SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("PID", "u", property_pid, 0,
        SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Paused", "b", property_boolean, 0,
        SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("SourceAvailable", "b", property_boolean, 0,
        SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("ActivityWhileRunning", "b", property_boolean, 0,
        SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("ActivityWhilePaused", "b", property_boolean, 0,
        SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_METHOD("Pause", "", "", unsupported_method,
        SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Resume", "", "", unsupported_method,
        SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

int input_proxy_runtime_control_derive_service_name(char *service_name,
    size_t service_name_size, const char *instance_name)
{
    int length;

    if (service_name == NULL || instance_name == NULL) {
        return -EINVAL;
    }
    length = snprintf(service_name, service_name_size, "%s%s",
        SERVICE_PREFIX, instance_name);
    if (length < 0 || (size_t)length >= service_name_size) {
        return -ENOBUFS;
    }
    return sd_bus_service_name_is_valid(service_name) > 0 ? 0 : -EINVAL;
}

enum input_proxy_runtime_control_failure
input_proxy_runtime_control_classify_connection_failure(int error_number)
{
    return error_number == EACCES || error_number == EPERM
        ? INPUT_PROXY_RUNTIME_CONTROL_CONNECTION_REJECTED
        : INPUT_PROXY_RUNTIME_CONTROL_SYSTEM_BUS_UNAVAILABLE;
}

enum input_proxy_runtime_control_failure
input_proxy_runtime_control_classify_name_failure(int error_number)
{
    if (error_number == EEXIST || error_number == EALREADY) {
        return INPUT_PROXY_RUNTIME_CONTROL_NAME_OWNED;
    }
    if (error_number == EACCES || error_number == EPERM) {
        return INPUT_PROXY_RUNTIME_CONTROL_NAME_DENIED;
    }
    return INPUT_PROXY_RUNTIME_CONTROL_INITIALIZATION_FAILED;
}

static const char *failure_description(enum input_proxy_runtime_control_failure failure)
{
    switch (failure) {
        case INPUT_PROXY_RUNTIME_CONTROL_SYSTEM_BUS_UNAVAILABLE:
            return "system bus unavailable";
        case INPUT_PROXY_RUNTIME_CONTROL_CONNECTION_REJECTED:
            return "system bus connection rejected";
        case INPUT_PROXY_RUNTIME_CONTROL_NAME_DENIED:
            return "service-name ownership denied";
        case INPUT_PROXY_RUNTIME_CONTROL_NAME_OWNED:
            return "service name already owned";
        case INPUT_PROXY_RUNTIME_CONTROL_INVALID_IDENTIFIER:
            return "invalid derived D-Bus identifier (internal invariant failure)";
        case INPUT_PROXY_RUNTIME_CONTROL_INITIALIZATION_FAILED:
            return "object export or D-Bus initialization failed";
    }
    return "D-Bus initialization failed";
}

static void warn_failure(const char *stage, const char *service_name,
    enum input_proxy_runtime_control_failure failure, int error_number)
{
    fprintf(stderr, "input-proxy: warning: D-Bus runtime control disabled: "
        "%s during %s%s%s: %s\n", failure_description(failure), stage,
        service_name == NULL ? "" : " for ",
        service_name == NULL ? "" : service_name, strerror(error_number));
}

void input_proxy_runtime_control_destroy(struct input_proxy_runtime_control *control)
{
    if (control == NULL) {
        return;
    }
    control->object_slot = sd_bus_slot_unref(control->object_slot);
    control->bus = sd_bus_flush_close_unref(control->bus);
    free(control);
}

struct input_proxy_runtime_control *input_proxy_runtime_control_create(
    const struct input_proxy_runtime_control_state *state)
{
    struct input_proxy_runtime_control *control;
    enum input_proxy_runtime_control_failure failure;
    const char *stage;
    int result;

    if (state == NULL) {
        return NULL;
    }
    control = calloc(1, sizeof(*control));
    if (control == NULL) {
        warn_failure("allocation", NULL,
            INPUT_PROXY_RUNTIME_CONTROL_INITIALIZATION_FAILED, ENOMEM);
        return NULL;
    }
    control->state = state;
    result = input_proxy_runtime_control_derive_service_name(
        control->service_name, sizeof(control->service_name), state->instance_name);
    if (result < 0) {
        warn_failure("service-name derivation", NULL,
            INPUT_PROXY_RUNTIME_CONTROL_INVALID_IDENTIFIER, -result);
        goto error;
    }
    stage = "system-bus connection";
    result = sd_bus_open_system(&control->bus);
    if (result < 0) {
        failure = input_proxy_runtime_control_classify_connection_failure(-result);
        goto initialization_error;
    }
    stage = "runtime-object export";
    result = sd_bus_add_object_vtable(control->bus, &control->object_slot,
        OBJECT_PATH, INTERFACE_NAME, runtime_vtable, (void *)state);
    if (result < 0) {
        failure = INPUT_PROXY_RUNTIME_CONTROL_INITIALIZATION_FAILED;
        goto initialization_error;
    }
    stage = "service-name acquisition";
    result = sd_bus_request_name(control->bus, control->service_name, 0);
    if (result < 0) {
        failure = input_proxy_runtime_control_classify_name_failure(-result);
        goto initialization_error;
    }
    return control;

initialization_error:
    warn_failure(stage, control->service_name, failure, -result);
error:
    input_proxy_runtime_control_destroy(control);
    return NULL;
}

size_t input_proxy_runtime_control_apply_changes(
    struct input_proxy_runtime_control **control,
    struct input_proxy_runtime_control_state *state,
    const struct input_proxy_runtime_control_changes *changes)
{
    char *changed_properties[5];
    size_t changed_count = 0;
    int result;

    if (state == NULL || changes == NULL) {
        return 0;
    }

#define COMMIT_BOOLEAN(mask, member, property_name) \
    do { \
        if ((changes->properties & (mask)) != 0U && \
            state->member != changes->member) { \
            state->member = changes->member; \
            changed_properties[changed_count++] = (property_name); \
        } \
    } while (0)

    COMMIT_BOOLEAN(INPUT_PROXY_RUNTIME_CONTROL_PAUSED,
        paused, "Paused");
    COMMIT_BOOLEAN(INPUT_PROXY_RUNTIME_CONTROL_SOURCE_AVAILABLE,
        source_available, "SourceAvailable");
    COMMIT_BOOLEAN(INPUT_PROXY_RUNTIME_CONTROL_ACTIVITY_WHILE_RUNNING,
        activity_while_running, "ActivityWhileRunning");
    COMMIT_BOOLEAN(INPUT_PROXY_RUNTIME_CONTROL_ACTIVITY_WHILE_PAUSED,
        activity_while_paused, "ActivityWhilePaused");

#undef COMMIT_BOOLEAN

    if (changed_count == 0 || control == NULL || *control == NULL) {
        return changed_count;
    }

    changed_properties[changed_count] = NULL;
    result = sd_bus_emit_properties_changed_strv(
        (*control)->bus,
        OBJECT_PATH,
        INTERFACE_NAME,
        changed_properties
    );
    if (result < 0) {
        warn_failure("property notification", (*control)->service_name,
            INPUT_PROXY_RUNTIME_CONTROL_INITIALIZATION_FAILED, -result);
        input_proxy_runtime_control_destroy(*control);
        *control = NULL;
    }

    return changed_count;
}

void input_proxy_runtime_control_process(struct input_proxy_runtime_control **control)
{
    int result;

    if (control == NULL || *control == NULL) {
        return;
    }
    do {
        result = sd_bus_process((*control)->bus, NULL);
    } while (result > 0);
    if (result < 0) {
        warn_failure("message dispatch", (*control)->service_name,
            INPUT_PROXY_RUNTIME_CONTROL_INITIALIZATION_FAILED, -result);
        input_proxy_runtime_control_destroy(*control);
        *control = NULL;
    }
}

void input_proxy_runtime_control_wait(struct input_proxy_runtime_control **control,
    uint64_t timeout_usec)
{
    if (control == NULL || *control == NULL) {
        const struct timespec delay = {
            .tv_sec = (time_t)(timeout_usec / 1000000U),
            .tv_nsec = (long)((timeout_usec % 1000000U) * 1000U)
        };
        (void)nanosleep(&delay, NULL);
        return;
    }
    {
        const int result = sd_bus_wait((*control)->bus, timeout_usec);
        if (result == -EINTR) {
            return;
        }
        if (result < 0) {
            warn_failure("message wait", (*control)->service_name,
                INPUT_PROXY_RUNTIME_CONTROL_INITIALIZATION_FAILED, -result);
            input_proxy_runtime_control_destroy(*control);
            *control = NULL;
            return;
        }
    }
    input_proxy_runtime_control_process(control);
}
