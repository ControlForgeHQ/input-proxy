#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "device_inspection_internal.h"
#include "libinput_status_internal.h"
#include "device_discovery_internal.h"
#include "runtime_discovery_internal.h"

#include <libevdev/libevdev.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <grp.h>
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

static bool safe_account_name(const char *value)
{
    size_t index;

    if (value == NULL || value[0] == '\0') return false;
    for (index = 0; value[index] != '\0'; ++index) {
        const unsigned char character = (unsigned char)value[index];
        if (!isalnum(character) && character != '_' && character != '-' &&
            character != '.')
            return false;
    }
    return true;
}

static bool supplementary_group_member(gid_t group)
{
    gid_t *groups;
    int count;
    int index;
    bool member = getegid() == group;

    if (member) return true;
    count = getgroups(0, NULL);
    if (count <= 0) return false;
    groups = malloc((size_t)count * sizeof(*groups));
    if (groups == NULL) return false;
    count = getgroups(count, groups);
    for (index = 0; index < count; ++index) {
        if (groups[index] == group) {
            member = true;
            break;
        }
    }
    free(groups);
    return member;
}

static const char *group_name(gid_t group, char *buffer, size_t size)
{
    const struct group *entry = getgrgid(group);

    if (entry == NULL || entry->gr_name == NULL || entry->gr_name[0] == '\0')
        return NULL;
    snprintf(buffer, size, "%s", entry->gr_name);
    return buffer;
}

static void print_shell_quoted(FILE *stream, const char *value)
{
    size_t index;

    fputc('\'', stream);
    for (index = 0; value[index] != '\0'; ++index) {
        if (value[index] == '\'') fputs("'\\''", stream);
        else fputc(value[index], stream);
    }
    fputc('\'', stream);
}

void input_proxy_print_access_remediation(
    FILE *stream, const struct input_proxy_access_remediation *access)
{
    if (stream == NULL || access == NULL ||
        (access->source_ok && access->uinput_ok))
        return;

    print_heading(stream, "Runtime accessibility remediation");
    fputc('\n', stream);
    if (!access->source_ok && access->source_group != NULL &&
        access->source_group_readable && !access->source_group_member &&
        safe_account_name(access->source_group) &&
        safe_account_name(access->user)) {
        print_heading(stream, "  Source access");
        fprintf(stream,
            "    Group \"%s\" can read this device, but user \"%s\" is not a member.\n"
            "\n"
            "    Suggested command:\n"
            "      sudo usermod -aG %s %s\n"
            "\n"
            "    %s: start a new login session before testing again.\n",
            access->source_group, access->user, access->source_group,
            access->user, semantic_status(stream, "NOTE", "33"));
    } else if (!access->source_ok) {
        print_heading(stream, "  Source access");
        fprintf(stream,
            "    %s: no safe permission change can be determined from the device's\n"
            "             current ownership and mode.\n"
            "\n"
            "    Check the source permissions and current user membership:\n"
            "\n",
            semantic_status(stream, "WARNING", "33"));
        if (access->source_path != NULL) {
            fputs("      ls -l -- ", stream);
            print_shell_quoted(stream, access->source_path);
            fputc('\n', stream);
        }
        fputs("      id\n", stream);
    }

    if (!access->source_ok && !access->uinput_ok) fputc('\n', stream);
    if (!access->uinput_exists) {
        print_heading(stream, "  /dev/uinput access");
        if (!access->uinput_module_loaded)
            fputs("    The device node is missing and the uinput module is not loaded in the\n"
                  "    running kernel.\n"
                  "\n"
                  "    Suggested command:\n"
                  "      sudo modprobe uinput\n", stream);
        else
            fprintf(stream,
                "    The uinput module is loaded in the running kernel, but the device node\n"
                "    is missing.\n"
                "\n"
                "    %s: no safe corrective command can be determined from this state.\n",
                semantic_status(stream, "WARNING", "33"));
    } else if (!access->uinput_ok && access->uinput_group != NULL &&
               access->uinput_group_writable &&
               !access->uinput_group_member &&
               safe_account_name(access->uinput_group) &&
               safe_account_name(access->user)) {
        print_heading(stream, "  /dev/uinput access");
        fprintf(stream,
            "    Group \"%s\" can write this device, but user \"%s\" is not a member.\n"
            "\n"
            "    Suggested command:\n"
            "      sudo usermod -aG %s %s\n"
            "\n"
            "    %s: start a new login session before testing again.\n",
            access->uinput_group, access->user, access->uinput_group,
            access->user, semantic_status(stream, "NOTE", "33"));
    } else if (!access->uinput_ok && access->input_group_available) {
        print_heading(stream, "  /dev/uinput access");
        fputs("    The device exists, but the current user cannot open it for writing.\n"
              "\n"
              "    Suggested udev rule:\n"
              "      KERNEL==\"uinput\", GROUP=\"input\", MODE=\"0660\", OPTIONS+=\"static_node=uinput\"\n"
              "\n"
              "    Suggested udev rule activation commands:\n"
              "      sudo udevadm control --reload-rules\n"
              "      sudo udevadm trigger --name-match=uinput\n",
              stream);
    } else if (!access->uinput_ok) {
        print_heading(stream, "  /dev/uinput access");
        fprintf(stream,
            "    The device exists, but the current user cannot open it for writing.\n"
            "\n"
            "    %s: no safe permission change can be determined from the device's\n"
            "             current ownership and mode.\n",
            semantic_status(stream, "WARNING", "33"));
    }
    fputc('\n', stream);
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
    const char *preferred_source)
{
    size_t index;
    bool heading_printed = false;

    if (stream == NULL || snapshot == NULL || event_node == NULL) {
        return;
    }
    if (!snapshot->available) {
        fputs("Runtime instance information unavailable: system D-Bus could "
            "not be queried.\n\n", stream);
        return;
    }
    for (index = 0; index < snapshot->record_count; ++index) {
        if (!input_proxy_runtime_record_matches_device(
                &snapshot->records[index], event_node, preferred_source)) {
            continue;
        }
        if (!heading_printed) {
            print_heading(stream, "Associated proxy instances");
            heading_printed = true;
        }
        fprintf(stream, "  %s [%s]\n",
            snapshot->records[index].instance_name,
            snapshot->records[index].source_path);
    }
    if (heading_printed) {
        fputc('\n', stream);
    }
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

enum input_proxy_result input_proxy_inspect_device(
    FILE *stream, FILE *error_stream, const char *device_path,
    const char *sysfs_input_path, const char *device_input_path,
    const char *uinput_path,
    const char *udev_data_path,
    const struct input_proxy_runtime_snapshot *runtime_snapshot)
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
    bool uinput_status_available = false;
    struct stat uinput_status;
    char source_group[128];
    char uinput_group[128];
    const struct passwd *user_entry;
    struct input_proxy_access_remediation remediation;
    struct input_proxy_device_rule_identity rule_identity = {0};
    size_t associated_instance_count;

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

    source_fd = open(device_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (source_fd >= 0 && libevdev_new_from_fd(source_fd, &device) == 0)
        source_ok = true;
    uinput_fd = open(uinput_path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (uinput_fd >= 0) uinput_ok = true;
    uinput_exists = stat(uinput_path, &uinput_status) == 0;
    uinput_status_available = uinput_exists;
    (void)input_proxy_find_persistent_input_path(device_input_path, &status,
                               persistent_path, sizeof(persistent_path));
    associated_instance_count = input_proxy_runtime_association_count(
        runtime_snapshot, event_node, persistent_path);

    print_heading(stream, "Device identity");
    print_value(stream, "Path:", device_path);
    print_value(stream, "Event node:", event_node);
    print_value(stream, "Preferred run source:", persistent_path);
    print_value(stream, "Name:", source_ok ? libevdev_get_name(device) : identity.name);
    print_value(stream, "Classification:", identity.classification);
    print_value(stream, "Origin:", identity.virtual_device ? "Virtual" : "Physical");
    print_value(stream, "Bus:", identity.bus);
    if (source_ok) {
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
    fprintf(stream, "  %-22s %s", "Source readable:",
            semantic_status(stream, source_ok ? "Yes" : "No",
                            source_ok ? "32" : "31"));
    fputc('\n', stream);
    {
        fprintf(stream, "  %-22s %s\n", "/dev/uinput exists:",
                semantic_status(stream, uinput_exists ? "Yes" : "No",
                                uinput_exists ? "32" : "31"));
    }
    if (!uinput_exists)
        fprintf(stream, "  %-22s %s\n", "/dev/uinput writable:",
                semantic_status(stream, "Unavailable", "31"));
    else
        fprintf(stream, "  %-22s %s\n", "/dev/uinput writable:",
                semantic_status(stream, uinput_ok ? "Yes" : "No",
                                uinput_ok ? "32" : "31"));

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
    if (!source_ok || !uinput_ok)
        fprintf(stream, "  %s: runtime access issues must be resolved.\n",
                semantic_status(stream, "NOT READY", "31"));
    else if (!ignored && !identity.virtual_device)
        fprintf(stream, "  %s: runtime access is available; libinput ignore is not configured.\n",
                semantic_status(stream, "READY WITH WARNINGS", "33"));
    else
        fprintf(stream, "  %s: device appears ready for input-proxy run.\n",
                semantic_status(stream, "READY", "32"));

    user_entry = getpwuid(geteuid());
    remediation.source_ok = source_ok;
    remediation.source_path = device_path;
    remediation.source_group = group_name(status.st_gid, source_group,
                                           sizeof(source_group));
    remediation.source_group_readable = (status.st_mode & S_IRGRP) != 0;
    remediation.source_group_member = supplementary_group_member(status.st_gid);
    remediation.uinput_exists = uinput_exists;
    remediation.uinput_ok = uinput_ok;
    remediation.uinput_group = uinput_status_available
        ? group_name(uinput_status.st_gid, uinput_group, sizeof(uinput_group))
        : NULL;
    remediation.uinput_group_writable = uinput_status_available &&
        (uinput_status.st_mode & S_IWGRP) != 0;
    remediation.uinput_group_member = uinput_status_available &&
        supplementary_group_member(uinput_status.st_gid);
    remediation.uinput_module_loaded = access("/sys/module/uinput", F_OK) == 0;
    remediation.input_group_available = getgrnam("input") != NULL;
    remediation.user = user_entry == NULL ? NULL : user_entry->pw_name;
    if (!source_ok || !uinput_ok) {
        fputc('\n', stream);
        input_proxy_print_access_remediation(stream, &remediation);
    }

    if (!ignored && !identity.virtual_device) {
        fputc('\n', stream);
        print_heading(stream, "Libinput remediation");
        fprintf(stream, "\n  %s: the physical source may also be consumed directly by libinput.\n",
                semantic_status(stream, "WARNING", "33"));
        if (input_proxy_rule_identity_has_udev_identity(&rule_identity) &&
            input_proxy_rule_value_is_safe(rule_identity.path)) {
            fprintf(stream,
                "\n  Suggested udev rule:\n"
                "    ACTION==\"add|change\", SUBSYSTEM==\"input\", KERNEL==\"event*\", "
                "ENV{ID_VENDOR_ID}==\"%s\", ENV{ID_MODEL_ID}==\"%s\", "
                "ENV{ID_PATH}==\"%s\", ENV{LIBINPUT_IGNORE_DEVICE}=\"1\"\n",
                rule_identity.udev_vendor, rule_identity.udev_model,
                rule_identity.path);
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
        print_heading(stream, "Suggested input-proxy run command");
        fprintf(stream, "  input-proxy run --source %s --name YOUR_INSTANCE_NAME\n",
                persistent_path[0] != '\0' ? persistent_path : device_path);
    }
    fputc('\n', stream);

    input_proxy_print_runtime_associations(stream, runtime_snapshot,
        event_node, persistent_path);

    if (device != NULL) libevdev_free(device);
    if (source_fd >= 0) close(source_fd);
    if (uinput_fd >= 0) close(uinput_fd);
    return INPUT_PROXY_SUCCESS;
}
