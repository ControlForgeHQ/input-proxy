#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "device_inspection_internal.h"
#include "deployment_readiness_internal.h"
#include "libinput_status_internal.h"
#include "device_discovery_internal.h"
#include "runtime_discovery_internal.h"
#include "installed_instance_internal.h"

#include <libevdev/libevdev.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#define REPORT_VALUE_COLUMN 25U
#define REPORT_WIDTH 79U

static bool color_enabled(FILE *stream)
{
    return getenv("NO_COLOR") == NULL && isatty(fileno(stream));
}

static const char *semantic_status(FILE *stream, const char *status,
                                   const char *color)
{
    static char colored[64];

    if (!color_enabled(stream)) return status;
    snprintf(colored, sizeof(colored), "\033[%sm%s\033[0m", color, status);
    return colored;
}

static void print_heading(FILE *stream, const char *heading)
{
    if (color_enabled(stream))
        fprintf(stream, "\033[1m%s\033[0m\n", heading);
    else
        fprintf(stream, "%s\n", heading);
}

static void begin_list(FILE *stream, const char *label, size_t *column)
{
    fprintf(stream, "  %-22s ", label);
    *column = REPORT_VALUE_COLUMN;
}

static void append_list_value(FILE *stream, const char *value, bool *any,
                              size_t *column)
{
    const size_t separator = *any ? 2U : 0U;
    const size_t length = strlen(value);

    if (*any && *column + separator + length > REPORT_WIDTH) {
        fprintf(stream, ",\n%*s", (int)REPORT_VALUE_COLUMN, "");
        *column = REPORT_VALUE_COLUMN;
        *any = false;
    }
    if (*any) {
        fputs(", ", stream);
        *column += 2U;
    }
    fputs(value, stream);
    *column += length;
    *any = true;
}

static void end_list(FILE *stream, bool any)
{
    fputs(any ? "\n" : "None\n", stream);
}

void input_proxy_print_wrapped_values(FILE *stream, const char *label,
                                      const char *const values[],
                                      size_t value_count)
{
    size_t index;
    size_t column;
    bool any = false;

    begin_list(stream, label, &column);
    for (index = 0; index < value_count; ++index)
        append_list_value(stream, values[index], &any, &column);
    end_list(stream, any);
}

static bool event_name(const char *path, const char **name)
{
    const char *base = strrchr(path, '/');
    size_t index;

    base = base == NULL ? path : base + 1;
    if (strncmp(base, "event", 5) != 0 || base[5] == '\0') return false;
    for (index = 5; base[index] != '\0'; ++index) {
        if (!isdigit((unsigned char)base[index])) return false;
    }
    *name = base;
    return true;
}

static void print_value(FILE *stream, const char *label, const char *value)
{
    fprintf(stream, "  %-22s %s\n", label,
            value != NULL && value[0] != '\0' ? value : "Unavailable");
}

static void print_hex(FILE *stream, const char *label, int value)
{
    if (value > 0) fprintf(stream, "  %-22s %04x\n", label, value);
    else print_value(stream, label, NULL);
}

static void print_codes(FILE *stream, const struct libevdev *device,
                        unsigned int type, const char *label)
{
    unsigned int code;
    bool any = false;
    size_t column;

    begin_list(stream, label, &column);
    for (code = 0; code <= (unsigned int)libevdev_event_type_get_max(type);
         ++code) {
        const char *name;
        if (!libevdev_has_event_code(device, type, code)) continue;
        name = libevdev_event_code_get_name(type, code);
        append_list_value(stream, name == NULL ? "?" : name, &any, &column);
    }
    end_list(stream, any);
}

static void print_event_types(FILE *stream, const struct libevdev *device)
{
    unsigned int type;
    bool any = false;
    size_t column;

    begin_list(stream, "Event types:", &column);
    for (type = 0; type <= EV_MAX; ++type) {
        const char *name;
        if (!libevdev_has_event_type(device, type)) continue;
        name = libevdev_event_type_get_name(type);
        append_list_value(stream, name == NULL ? "?" : name, &any, &column);
    }
    end_list(stream, any);
}

bool input_proxy_find_persistent_input_path(const char *device_input_path,
                                 const struct stat *status,
                                 char *persistent_path, size_t path_size)
{
    static const char *const directories[] = {"by-id", "by-path"};
    size_t directory_index;

    for (directory_index = 0; directory_index < 2; ++directory_index) {
        char directory[PATH_MAX];
        struct dirent **entries = NULL;
        int count;
        int index;

        if (snprintf(directory, sizeof(directory), "%s/%s", device_input_path,
                     directories[directory_index]) >= (int)sizeof(directory))
            continue;
        count = scandir(directory, &entries, NULL, alphasort);
        if (count < 0) continue;
        for (index = 0; index < count; ++index) {
            char candidate[PATH_MAX];
            struct stat candidate_status;
            bool match = false;
            if (entries[index]->d_name[0] != '.' &&
                snprintf(candidate, sizeof(candidate), "%s/%s", directory,
                         entries[index]->d_name) < (int)sizeof(candidate) &&
                stat(candidate, &candidate_status) == 0 &&
                S_ISCHR(candidate_status.st_mode) &&
                candidate_status.st_rdev == status->st_rdev) {
                snprintf(persistent_path, path_size, "%s", candidate);
                match = true;
            }
            free(entries[index]);
            if (match) {
                while (++index < count) free(entries[index]);
                free(entries);
                return true;
            }
        }
        free(entries);
    }
    return false;
}

bool input_proxy_resolve_event_node(const char *sysfs_input_path,
                               const char *device_input_path,
                               const struct stat *device_status,
                               char *event_node, size_t event_node_size,
                               char *event_sysfs_path, size_t sysfs_path_size)
{
    struct dirent **entries = NULL;
    int count;
    int index;

    count = scandir(sysfs_input_path, &entries, NULL, alphasort);
    if (count < 0) return false;
    for (index = 0; index < count; ++index) {
        char dev_path[PATH_MAX];
        const char *ignored_name;
        FILE *file;
        unsigned int device_major;
        unsigned int device_minor;
        bool match = false;

        if (event_name(entries[index]->d_name, &ignored_name) &&
            snprintf(dev_path, sizeof(dev_path), "%s/%s/dev", sysfs_input_path,
                     entries[index]->d_name) < (int)sizeof(dev_path) &&
            (file = fopen(dev_path, "r")) != NULL) {
            if (fscanf(file, "%u:%u", &device_major, &device_minor) == 2 &&
                device_major == major(device_status->st_rdev) &&
                device_minor == minor(device_status->st_rdev) &&
                snprintf(event_node, event_node_size, "%s/%s", device_input_path,
                         entries[index]->d_name) < (int)event_node_size &&
                snprintf(event_sysfs_path, sysfs_path_size, "%s/%s",
                         sysfs_input_path, entries[index]->d_name) <
                    (int)sysfs_path_size)
                match = true;
            fclose(file);
        }
        free(entries[index]);
        if (match) {
            while (++index < count) free(entries[index]);
            free(entries);
            return true;
        }
    }
    free(entries);
    return false;
}

bool input_proxy_rule_value_is_safe(const char *value)
{
    size_t index;

    if (value[0] == '\0') return false;
    for (index = 0; value[index] != '\0'; ++index) {
        const unsigned char character = (unsigned char)value[index];
        if (!isalnum(character) && character != '_' && character != '-' &&
            character != '.' && character != ':' && character != '/')
            return false;
    }
    return true;
}

void input_proxy_collect_rule_identity(const char *name, const char *value,
                                       void *userdata)
{
    struct input_proxy_device_rule_identity *identity = userdata;

    if (identity == NULL) return;
    if (strcmp(name, "ID_VENDOR_ID") == 0)
        snprintf(identity->udev_vendor, sizeof(identity->udev_vendor), "%s",
                 value);
    else if (strcmp(name, "ID_MODEL_ID") == 0)
        snprintf(identity->udev_model, sizeof(identity->udev_model), "%s",
                 value);
    else if (strcmp(name, "ID_PATH") == 0)
        snprintf(identity->path, sizeof(identity->path), "%s", value);
}

void input_proxy_rule_identity_add_kernel_identity(
    struct input_proxy_device_rule_identity *rule_identity,
    const struct input_proxy_device_identity *device_identity)
{
    if (rule_identity == NULL || device_identity == NULL) return;
    snprintf(rule_identity->bus, sizeof(rule_identity->bus), "%s",
             device_identity->bus_id);
    snprintf(rule_identity->vendor, sizeof(rule_identity->vendor), "%s",
             device_identity->vendor_id);
    snprintf(rule_identity->product, sizeof(rule_identity->product), "%s",
             device_identity->product_id);
}

bool input_proxy_rule_identity_has_udev_identity(
    const struct input_proxy_device_rule_identity *identity)
{
    return identity != NULL &&
        input_proxy_rule_value_is_safe(identity->udev_vendor) &&
        input_proxy_rule_value_is_safe(identity->udev_model);
}

bool input_proxy_rule_identity_is_narrow(
    const struct input_proxy_device_rule_identity *identity)
{
    return identity != NULL &&
        input_proxy_rule_value_is_safe(identity->path) &&
        (input_proxy_rule_identity_has_udev_identity(identity) ||
         (input_proxy_rule_value_is_safe(identity->bus) &&
          input_proxy_rule_value_is_safe(identity->vendor) &&
          input_proxy_rule_value_is_safe(identity->product)));
}

char *input_proxy_render_libinput_ignore_rule(
    const struct input_proxy_device_rule_identity *identity)
{
    char identity_match[512];
    int size;
    char *rule;

    if (!input_proxy_rule_identity_is_narrow(identity)) return NULL;
    if (input_proxy_rule_identity_has_udev_identity(identity))
        (void)snprintf(identity_match, sizeof(identity_match),
            "ENV{ID_VENDOR_ID}==\"%s\", ENV{ID_MODEL_ID}==\"%s\"",
            identity->udev_vendor, identity->udev_model);
    else
        (void)snprintf(identity_match, sizeof(identity_match),
            "ATTRS{id/bustype}==\"%s\", ATTRS{id/vendor}==\"%s\", ATTRS{id/product}==\"%s\"",
            identity->bus, identity->vendor, identity->product);
    size = snprintf(NULL, 0,
        "ACTION==\"add|change\", SUBSYSTEM==\"input\", KERNEL==\"event*\", "
        "%s, ENV{ID_PATH}==\"%s\", ENV{LIBINPUT_IGNORE_DEVICE}=\"1\"\n",
        identity_match, identity->path);
    if (size < 0) return NULL;
    rule = malloc((size_t)size + 1);
    if (rule == NULL) return NULL;
    (void)snprintf(rule, (size_t)size + 1,
        "ACTION==\"add|change\", SUBSYSTEM==\"input\", KERNEL==\"event*\", "
        "%s, ENV{ID_PATH}==\"%s\", ENV{LIBINPUT_IGNORE_DEVICE}=\"1\"\n",
        identity_match, identity->path);
    return rule;
}

void input_proxy_print_access_diagnostics(
    FILE *stream, const struct input_proxy_access_diagnostics *access)
{
    const bool current_failure = access != NULL &&
        (!access->current_source_ok || !access->current_uinput_ok);
    const bool service_failure = access != NULL &&
        (access->service_identity_result !=
            INPUT_PROXY_INSTALL_SERVICE_IDENTITY_VALID ||
         !access->service_source_ok || !access->service_uinput_ok);

    if (stream == NULL || access == NULL ||
        (!current_failure && !service_failure))
        return;

    print_heading(stream, "Runtime accessibility diagnostics");
    fputc('\n', stream);
    if (current_failure) {
        print_heading(stream, "  Current-user access");
    }
    if (!access->current_source_ok) {
        fputs("    The current user cannot read this input device.\n", stream);
    }
    if (!access->current_source_ok && !access->current_uinput_ok) fputc('\n', stream);
    if (!access->current_uinput_ok) {
        fputs(access->uinput_exists
            ? "    /dev/uinput is not writable by the current user.\n"
            : "    /dev/uinput is unavailable.\n", stream);
    }
    if (current_failure && service_failure) fputc('\n', stream);

    if (access->service_identity_result != INPUT_PROXY_INSTALL_SERVICE_IDENTITY_VALID) {
        print_heading(stream, "  Service-identity access");
        fputs("    The input-proxy service identity is unavailable or incomplete.\n\n"
              "    Installed Instance execution requires the package-owned service user,\n"
              "    primary group, and supplementary input-group membership.\n\n"
              "    Check package integration.\n", stream);
    } else if (!access->service_source_ok || !access->service_uinput_ok) {
        print_heading(stream, "  Service-identity access");
        if (!access->service_source_ok)
            fputs("    The input-proxy service identity cannot read this input device.\n",
                  stream);
        if (!access->service_source_ok && !access->service_uinput_ok)
            fputc('\n', stream);
        if (!access->service_uinput_ok)
            fputs("    The input-proxy service identity cannot open /dev/uinput for writing.\n",
                  stream);
        fputs("\n    Installed Instance execution requires this access through package-owned\n"
              "    host integration.\n\n"
              "    Check package integration.\n", stream);
    }
}

bool input_proxy_should_suggest_run(
    bool source_accessible,
    bool uinput_accessible,
    size_t associated_instance_count)
{
    return source_accessible && uinput_accessible &&
        associated_instance_count == 0;
}

void input_proxy_print_runtime_associations(
    FILE *stream,
    const struct input_proxy_runtime_snapshot *snapshot,
    const char *event_node,
    const char *preferred_source,
    const struct input_proxy_installed_instance_store *installed_instances)
{
    size_t index;
    bool heading_printed = false;
    bool registry_available = false;
    struct input_proxy_installed_instance_list installed = {0};

    if (stream == NULL || snapshot == NULL || event_node == NULL) {
        return;
    }
    if (!snapshot->available) {
        fputs("Runtime instance information unavailable: system D-Bus could "
            "not be queried.\n", stream);
        return;
    }
    if (installed_instances != NULL &&
        input_proxy_installed_instance_enumerate(installed_instances,
            &installed) == INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS)
        registry_available = true;
    for (index = 0; index < snapshot->record_count; ++index) {
        if (!input_proxy_runtime_record_matches_device(
                &snapshot->records[index], event_node, preferred_source)) {
            continue;
        }
        if (!heading_printed) {
            print_heading(stream, "Associated proxy instances");
            heading_printed = true;
        }
        {
            bool instance_installed = false;
            const char *classification = "Unknown";
            size_t installed_index;
            for (installed_index = 0; installed_index < installed.count;
                 ++installed_index) {
                if (strcmp(installed.names[installed_index],
                        snapshot->records[index].instance_name) == 0)
                    instance_installed = true;
            }
            if (registry_available)
                classification = instance_installed
                    ? "Installed" : "Direct-run";
            fprintf(stream, "  %s [%s] [%s]\n",
                snapshot->records[index].instance_name,
                classification,
                snapshot->records[index].source_path);
        }
    }
    input_proxy_installed_instance_list_destroy(&installed);
}

static void resolve_effective_user(char *identity, size_t identity_size)
{
    const uid_t uid = geteuid();
    struct passwd account;
    struct passwd *resolved = NULL;
    long buffer_size = sysconf(_SC_GETPW_R_SIZE_MAX);
    char *buffer;

    if (buffer_size < 0) buffer_size = 16384;
    buffer = malloc((size_t)buffer_size);
    if (buffer != NULL &&
        getpwuid_r(uid, &account, buffer, (size_t)buffer_size, &resolved) == 0 &&
        resolved != NULL && resolved->pw_name != NULL) {
        (void)snprintf(identity, identity_size, "%s", resolved->pw_name);
    } else {
        (void)snprintf(identity, identity_size, "%ju", (uintmax_t)uid);
    }
    free(buffer);
}

struct inspection_udev_properties {
    FILE *stream;
    struct input_proxy_device_rule_identity *rule_identity;
};

static void collect_udev_property(const char *name, const char *value,
                                  void *userdata)
{
    struct inspection_udev_properties *properties = userdata;

    input_proxy_collect_rule_identity(name, value, properties->rule_identity);
    if (strncmp(name, "ID_INPUT", 8) == 0 || strcmp(name, "ID_PATH") == 0 ||
        strcmp(name, "LIBINPUT_IGNORE_DEVICE") == 0)
        fprintf(properties->stream, "  %-22s %s\n", name, value);
}

enum input_proxy_result input_proxy_inspect_device_with_service_environment(
    FILE *stream, FILE *error_stream, const char *device_path,
    const char *sysfs_input_path, const char *device_input_path,
    const char *uinput_path,
    const char *udev_data_path,
    const struct input_proxy_runtime_snapshot *runtime_snapshot,
    enum input_proxy_install_service_identity_result service_identity_result,
    const struct input_proxy_deployment_environment *service_environment,
    const struct input_proxy_installed_instance_store *installed_instances)
{
    char event_node[PATH_MAX];
    char sysfs_path[PATH_MAX];
    char persistent_path[PATH_MAX] = "";
    struct stat status;
    struct input_proxy_device_identity identity;
    struct libevdev *device = NULL;
    int source_fd = -1;
    int uinput_fd = -1;
    bool source_ok = false;
    bool uinput_ok = false;
    bool ignored = false;
    enum input_proxy_libinput_status libinput_status;
    bool uinput_exists = false;
    bool service_source_ok = false;
    bool service_uinput_ok = false;
    struct stat uinput_status;
    struct input_proxy_access_diagnostics access_diagnostics;
    struct input_proxy_device_rule_identity rule_identity = {0};
    size_t associated_instance_count;
    char current_user[256];
    char access_heading[320];
    const char *service_name = "input-proxy";

    if (stream == NULL || error_stream == NULL || device_path == NULL ||
        sysfs_input_path == NULL || uinput_path == NULL || udev_data_path == NULL)
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    if (device_input_path == NULL || stat(device_path, &status) != 0 ||
        !S_ISCHR(status.st_mode)) {
        fprintf(error_stream, "input-proxy: '%s' is not an input event device\n",
                device_path);
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }
    if (!input_proxy_resolve_event_node(sysfs_input_path, device_input_path, &status,
                            event_node, sizeof(event_node), sysfs_path,
                            sizeof(sysfs_path))) {
        fprintf(error_stream, "input-proxy: '%s' is not an input event device\n",
                device_path);
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }
    if (!input_proxy_read_device_identity(sysfs_path, &identity)) {
        fprintf(error_stream, "input-proxy: cannot inspect event metadata for '%s'\n",
                device_path);
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }
    input_proxy_rule_identity_add_kernel_identity(&rule_identity, &identity);
    resolve_effective_user(current_user, sizeof(current_user));
    if (service_environment != NULL && service_environment->service_name != NULL)
        service_name = service_environment->service_name;

    source_fd = open(device_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (source_fd >= 0) {
        source_ok = true;
        (void)libevdev_new_from_fd(source_fd, &device);
    }
    uinput_fd = open(uinput_path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (uinput_fd >= 0) uinput_ok = true;
    uinput_exists = stat(uinput_path, &uinput_status) == 0;
    if (service_identity_result == INPUT_PROXY_INSTALL_SERVICE_IDENTITY_VALID &&
        service_environment != NULL) {
        service_source_ok = input_proxy_deployment_identity_has_access(
            &status, service_environment, S_IRUSR, S_IRGRP, S_IROTH);
        service_uinput_ok = uinput_exists && S_ISCHR(uinput_status.st_mode) &&
            input_proxy_deployment_identity_has_access(&uinput_status,
                service_environment, S_IWUSR, S_IWGRP, S_IWOTH);
    }
    (void)input_proxy_find_persistent_input_path(device_input_path, &status,
                               persistent_path, sizeof(persistent_path));
    associated_instance_count = input_proxy_runtime_association_count(
        runtime_snapshot, event_node, persistent_path);

    print_heading(stream, "Device identity");
    print_value(stream, "Path:", device_path);
    print_value(stream, "Event node:", event_node);
    print_value(stream, "Preferred run source:", persistent_path);
    print_value(stream, "Name:", device != NULL ? libevdev_get_name(device) : identity.name);
    print_value(stream, "Classification:", identity.classification);
    print_value(stream, "Origin:", identity.virtual_device ? "Virtual" : "Physical");
    print_value(stream, "Bus:", identity.bus);
    if (device != NULL) {
        print_hex(stream, "Vendor:", libevdev_get_id_vendor(device));
        print_hex(stream, "Product:", libevdev_get_id_product(device));
        print_hex(stream, "Version:", libevdev_get_id_version(device));
        print_value(stream, "Physical path:", libevdev_get_phys(device));
        print_value(stream, "Unique identifier:", libevdev_get_uniq(device));

        fputc('\n', stream);
        print_heading(stream, "Input characteristics");
        print_event_types(stream, device);
        print_codes(stream, device, EV_KEY, "Keys/buttons:");
        print_codes(stream, device, EV_REL, "Relative axes:");
        print_codes(stream, device, EV_ABS, "Absolute axes:");
        {
            size_t column;
            begin_list(stream, "Input properties:", &column);
            {
                unsigned int property;
                bool any = false;
                for (property = 0; property <= INPUT_PROP_MAX; ++property) {
                    const char *name;
                    if (!libevdev_has_property(device, property)) continue;
                    name = libevdev_property_get_name(property);
                    append_list_value(stream, name ? name : "?", &any, &column);
                }
                end_list(stream, any);
            }
        }
        fprintf(stream, "  %-22s %s\n", "Multitouch:",
                libevdev_has_event_code(device, EV_ABS, ABS_MT_TRACKING_ID)
                    ? "Supported" : "Not detected");
        if (libevdev_has_event_code(device, EV_ABS, ABS_MT_SLOT)) {
            const struct input_absinfo *slots = libevdev_get_abs_info(device, ABS_MT_SLOT);
            if (slots != NULL) fprintf(stream, "  %-22s %d\n", "Multitouch slots:", slots->maximum + 1);
        } else print_value(stream, "Multitouch slots:", NULL);
    } else {
        fputc('\n', stream);
        print_heading(stream, "Input characteristics");
        fputs("  Capabilities:          Unavailable (source cannot be opened)\n", stream);
    }

    fputc('\n', stream);
    print_heading(stream, "Runtime accessibility");
    fputc('\n', stream);
    (void)snprintf(access_heading, sizeof(access_heading),
        "  Current user [%s]", current_user);
    print_heading(stream, access_heading);
    fprintf(stream, "    %-22s %s", "Source readable:",
            semantic_status(stream, source_ok ? "Yes" : "No",
                            source_ok ? "32" : "31"));
    fputc('\n', stream);
    {
        fprintf(stream, "    %-22s %s\n", "/dev/uinput exists:",
                semantic_status(stream, uinput_exists ? "Yes" : "No",
                                uinput_exists ? "32" : "31"));
    }
    if (!uinput_exists)
        fprintf(stream, "    %-22s %s\n", "/dev/uinput writable:",
                semantic_status(stream, "Unavailable", "31"));
    else
        fprintf(stream, "    %-22s %s\n", "/dev/uinput writable:",
                semantic_status(stream, uinput_ok ? "Yes" : "No",
                                uinput_ok ? "32" : "31"));
    fputc('\n', stream);
    (void)snprintf(access_heading, sizeof(access_heading),
        "  Service identity [%s]", service_name);
    print_heading(stream, access_heading);
    fprintf(stream, "    %-22s %s\n", "Available:",
        semantic_status(stream,
            service_identity_result == INPUT_PROXY_INSTALL_SERVICE_IDENTITY_VALID
                ? "Yes" : "No",
            service_identity_result == INPUT_PROXY_INSTALL_SERVICE_IDENTITY_VALID
                ? "32" : "31"));
    if (service_identity_result == INPUT_PROXY_INSTALL_SERVICE_IDENTITY_VALID) {
        fprintf(stream, "    %-22s %s\n", "Source readable:",
            semantic_status(stream, service_source_ok ? "Yes" : "No",
                service_source_ok ? "32" : "31"));
        fprintf(stream, "    %-22s %s\n", "/dev/uinput writable:",
            semantic_status(stream, service_uinput_ok ? "Yes" : "No",
                service_uinput_ok ? "32" : "31"));
    }

    fputc('\n', stream);
    print_heading(stream, "Udev and libinput context");
    {
        struct inspection_udev_properties properties = {
            stream, &rule_identity
        };
        libinput_status = input_proxy_read_libinput_status(
            udev_data_path, &status, collect_udev_property, &properties);
        ignored = libinput_status == INPUT_PROXY_LIBINPUT_STATUS_IGNORED;
    }
    fprintf(stream, "  %-22s %s\n", "Libinput ignored:",
            semantic_status(stream, ignored ? "Yes" : "No",
                            ignored ? "32" : "33"));

    fputc('\n', stream);
    print_heading(stream, "Proxy readiness");
    fprintf(stream, "\n  %-22s %s\n", "Manual run:",
        semantic_status(stream, !source_ok || !uinput_ok ? "NOT READY" :
            (!ignored && !identity.virtual_device ? "READY WITH WARNINGS" : "READY"),
            !source_ok || !uinput_ok ? "31" :
            (!ignored && !identity.virtual_device ? "33" : "32")));
    fprintf(stream, "  %-22s %s\n", "Installed Instance:",
        semantic_status(stream,
            service_identity_result != INPUT_PROXY_INSTALL_SERVICE_IDENTITY_VALID ||
            !service_source_ok || !service_uinput_ok ? "NOT READY" :
            (!ignored && !identity.virtual_device ? "READY WITH WARNINGS" : "READY"),
            service_identity_result != INPUT_PROXY_INSTALL_SERVICE_IDENTITY_VALID ||
            !service_source_ok || !service_uinput_ok ? "31" :
            (!ignored && !identity.virtual_device ? "33" : "32")));

    access_diagnostics.current_source_ok = source_ok;
    access_diagnostics.current_uinput_ok = uinput_ok;
    access_diagnostics.uinput_exists = uinput_exists;
    access_diagnostics.service_identity_result = service_identity_result;
    access_diagnostics.service_source_ok = service_source_ok;
    access_diagnostics.service_uinput_ok = service_uinput_ok;
    if (!source_ok || !uinput_ok ||
        service_identity_result != INPUT_PROXY_INSTALL_SERVICE_IDENTITY_VALID ||
        !service_source_ok || !service_uinput_ok) {
        fputc('\n', stream);
        input_proxy_print_access_diagnostics(stream, &access_diagnostics);
    }

    if (!ignored && !identity.virtual_device) {
        fputc('\n', stream);
        print_heading(stream, "Libinput remediation");
        fprintf(stream, "\n  %s: the physical source may also be consumed directly by libinput.\n",
                semantic_status(stream, "WARNING", "33"));
        char *rule = input_proxy_render_libinput_ignore_rule(&rule_identity);
        if (rule != NULL) {
            fprintf(stream, "\n  Suggested udev rule:\n    %s", rule);
            free(rule);
            if (strcmp(identity.bus, "USB") == 0)
                fprintf(stream,
                      "\n  %s: this rule includes ID_PATH and is tied to the device's current\n"
                      "        physical connection path. Moving the device to another USB port\n"
                      "        may require updating the rule.\n",
                      semantic_status(stream, "NOTE", "33"));
            fputs("\n  Suggested udev rule activation commands:\n"
                  "    sudo udevadm control --reload-rules\n"
                  "    sudo udevadm trigger --subsystem-match=input --action=change\n",
                  stream);
        } else {
            fputs("\n  No udev rule suggested: a stable path plus vendor and model identifiers are unavailable.\n",
                  stream);
        }
    }

    if (input_proxy_should_suggest_run(
            source_ok, uinput_ok, associated_instance_count)) {
        fputc('\n', stream);
        print_heading(stream, "Suggested manual command");
        fprintf(stream, "  input-proxy run --source %s --name YOUR_INSTANCE_NAME\n",
                persistent_path[0] != '\0' ? persistent_path : device_path);
    }
    if (service_identity_result == INPUT_PROXY_INSTALL_SERVICE_IDENTITY_VALID &&
        service_source_ok && service_uinput_ok && associated_instance_count == 0) {
        fputc('\n', stream);
        print_heading(stream, "Suggested installation command");
        fprintf(stream, "  sudo input-proxy install --source %s --name YOUR_INSTANCE_NAME\n",
                persistent_path[0] != '\0' ? persistent_path : device_path);
    }
    if (runtime_snapshot != NULL &&
        (!runtime_snapshot->available || associated_instance_count > 0)) {
        fputc('\n', stream);
        input_proxy_print_runtime_associations(stream, runtime_snapshot,
            event_node, persistent_path, installed_instances);
    }

    if (device != NULL) libevdev_free(device);
    if (source_fd >= 0) close(source_fd);
    if (uinput_fd >= 0) close(uinput_fd);
    return INPUT_PROXY_SUCCESS;
}

enum input_proxy_result input_proxy_inspect_device(
    FILE *stream, FILE *error_stream, const char *device_path,
    const char *sysfs_input_path, const char *device_input_path,
    const char *uinput_path, const char *udev_data_path,
    const struct input_proxy_runtime_snapshot *runtime_snapshot)
{
    struct input_proxy_deployment_environment service_environment;
    struct input_proxy_installed_instance_store *installed_instances = NULL;
    gid_t *groups = NULL;
    enum input_proxy_install_service_identity_result identity_result =
        input_proxy_service_environment_resolve(&service_environment, &groups);
    enum input_proxy_result result;

    (void)input_proxy_installed_instance_store_create(&installed_instances);
    result = input_proxy_inspect_device_with_service_environment(
            stream, error_stream, device_path, sysfs_input_path,
            device_input_path, uinput_path, udev_data_path, runtime_snapshot,
            identity_result,
            identity_result == INPUT_PROXY_INSTALL_SERVICE_IDENTITY_VALID
                ? &service_environment : NULL,
            installed_instances);
    input_proxy_installed_instance_store_destroy(installed_instances);
    free(groups);
    return result;
}
