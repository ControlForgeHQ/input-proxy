#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "device_inspection_internal.h"
#include "device_discovery_internal.h"

#include <libevdev/libevdev.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
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

static bool find_persistent_path(const char *device_path, const struct stat *status,
                                 char *persistent_path, size_t path_size)
{
    static const char *const directories[] = {"by-id", "by-path"};
    char root[PATH_MAX];
    char *separator;
    size_t directory_index;

    if (strlen(device_path) >= sizeof(root)) return false;
    strcpy(root, device_path);
    separator = strrchr(root, '/');
    if (separator == NULL) return false;
    *separator = '\0';

    for (directory_index = 0; directory_index < 2; ++directory_index) {
        char directory[PATH_MAX];
        struct dirent **entries = NULL;
        int count;
        int index;

        if (snprintf(directory, sizeof(directory), "%s/%s", root,
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

static bool safe_rule_value(const char *value)
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

static bool read_udev_properties(const char *root, const struct stat *status,
                                 FILE *stream, char *vendor, size_t vendor_size,
                                 char *model, size_t model_size,
                                 char *device_id_path, size_t id_path_size)
{
    char path[PATH_MAX];
    char line[1024];
    FILE *file;
    bool ignored = false;

    if (!S_ISCHR(status->st_mode) || snprintf(path, sizeof(path), "%s/c%u:%u",
        root, major(status->st_rdev), minor(status->st_rdev)) >= (int)sizeof(path))
        return false;
    file = fopen(path, "r");
    if (file == NULL) return false;
    while (fgets(line, sizeof(line), file) != NULL) {
        char *value;
        size_t length = strlen(line);
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
            line[--length] = '\0';
        if (strncmp(line, "E:", 2) != 0) continue;
        value = strchr(line + 2, '=');
        if (value == NULL) continue;
        *value++ = '\0';
        if (strcmp(line + 2, "LIBINPUT_IGNORE_DEVICE") == 0 &&
            strcmp(value, "1") == 0) ignored = true;
        if (strcmp(line + 2, "ID_VENDOR_ID") == 0)
            snprintf(vendor, vendor_size, "%s", value);
        if (strcmp(line + 2, "ID_MODEL_ID") == 0)
            snprintf(model, model_size, "%s", value);
        if (strcmp(line + 2, "ID_PATH") == 0)
            snprintf(device_id_path, id_path_size, "%s", value);
        if (strncmp(line + 2, "ID_INPUT", 8) == 0 ||
            strcmp(line + 2, "ID_PATH") == 0 ||
            strcmp(line + 2, "LIBINPUT_IGNORE_DEVICE") == 0)
            fprintf(stream, "  %-22s %s\n", line + 2, value);
    }
    fclose(file);
    return ignored;
}

enum input_proxy_result input_proxy_inspect_device(
    FILE *stream, FILE *error_stream, const char *device_path,
    const char *sysfs_input_path, const char *uinput_path,
    const char *udev_data_path)
{
    const char *node_name;
    char canonical_device_path[PATH_MAX];
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
    char rule_vendor[32] = "";
    char rule_model[32] = "";
    char rule_path[512] = "";

    if (stream == NULL || error_stream == NULL || device_path == NULL ||
        sysfs_input_path == NULL || uinput_path == NULL || udev_data_path == NULL)
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    if (event_name(device_path, &node_name)) {
        if (snprintf(canonical_device_path, sizeof(canonical_device_path), "%s",
                     device_path) >= (int)sizeof(canonical_device_path))
            canonical_device_path[0] = '\0';
    } else if (realpath(device_path, canonical_device_path) == NULL ||
               !event_name(canonical_device_path, &node_name)) {
        canonical_device_path[0] = '\0';
    }
    if (canonical_device_path[0] == '\0' || stat(device_path, &status) != 0 ||
        !S_ISCHR(status.st_mode)) {
        fprintf(error_stream, "input-proxy: '%s' is not an input event device\n",
                device_path);
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }
    if (snprintf(sysfs_path, sizeof(sysfs_path), "%s/%s", sysfs_input_path,
                 node_name) >= (int)sizeof(sysfs_path) ||
        !input_proxy_read_device_identity(sysfs_path, &identity)) {
        fprintf(error_stream, "input-proxy: cannot inspect event metadata for '%s'\n",
                device_path);
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    source_fd = open(device_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (source_fd >= 0 && libevdev_new_from_fd(source_fd, &device) == 0)
        source_ok = true;
    uinput_fd = open(uinput_path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (uinput_fd >= 0) uinput_ok = true;
    (void)find_persistent_path(canonical_device_path, &status,
                               persistent_path, sizeof(persistent_path));

    fputs("Device identity\n", stream);
    print_value(stream, "Path:", device_path);
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

        fputs("\nInput characteristics\n", stream);
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
        fputs("\nInput characteristics\n  Capabilities:          Unavailable (source cannot be opened)\n", stream);
    }

    fputs("\nRuntime accessibility\n", stream);
    fprintf(stream, "  %-22s %s", "Source readable:", source_ok ? "Yes" : "No");
    if (!source_ok)
        fprintf(stream, " (%s)", semantic_status(stream, "BLOCKER", "31"));
    fputc('\n', stream);
    fprintf(stream, "  %-22s %s\n", "/dev/uinput exists:",
            access(uinput_path, F_OK) == 0 ? "Yes" : "No");
    fprintf(stream, "  %-22s %s", "/dev/uinput writable:", uinput_ok ? "Yes" : "No");
    if (!uinput_ok)
        fprintf(stream, " (%s)", semantic_status(stream, "BLOCKER", "31"));
    fputc('\n', stream);

    fputs("\nUdev and libinput context\n", stream);
    ignored = read_udev_properties(udev_data_path, &status, stream,
                                   rule_vendor, sizeof(rule_vendor),
                                   rule_model, sizeof(rule_model),
                                   rule_path, sizeof(rule_path));
    fprintf(stream, "  %-22s %s\n", "Libinput ignored:", ignored ? "Yes" : "No");
    if (!ignored && !identity.virtual_device) {
        fprintf(stream, "  %s: the physical source may also be consumed directly by libinput.\n",
                semantic_status(stream, "WARNING", "33"));
        if (safe_rule_value(rule_vendor) && safe_rule_value(rule_model) &&
            safe_rule_value(rule_path)) {
            fprintf(stream,
                "\n  Suggested udev rule (not applied):\n"
                "    ACTION==\"add|change\", SUBSYSTEM==\"input\", KERNEL==\"event*\", "
                "ENV{ID_VENDOR_ID}==\"%s\", ENV{ID_MODEL_ID}==\"%s\", "
                "ENV{ID_PATH}==\"%s\", ENV{LIBINPUT_IGNORE_DEVICE}=\"1\"\n"
                "\n  Suggested commands (not run):\n"
                "    sudo udevadm control --reload-rules\n"
                "    sudo udevadm trigger --subsystem-match=input --action=change\n",
                rule_vendor, rule_model, rule_path);
        } else {
            fputs("  No udev rule suggested: a stable path plus vendor and model identifiers are unavailable.\n", stream);
        }
    }

    fputs("\nProxy readiness\n", stream);
    if (!source_ok || !uinput_ok)
        fprintf(stream, "  %s: runtime access blockers must be resolved.\n",
                semantic_status(stream, "BLOCKED", "31"));
    else if (!ignored && !identity.virtual_device)
        fprintf(stream, "  %s: runtime access is available; libinput ignore is not configured.\n",
                semantic_status(stream, "READY WITH WARNINGS", "33"));
    else
        fprintf(stream, "  %s: device appears ready for input-proxy run.\n",
                semantic_status(stream, "READY", "32"));

    fputs("\nSuggested command\n\n  input-proxy run \\\n", stream);
    fprintf(stream, "      --source %s \\\n", persistent_path[0] != '\0'
            ? persistent_path : device_path);
    fputs("      --name \"DESIRED DEVICE NAME\"\n\n", stream);

    if (device != NULL) libevdev_free(device);
    if (source_fd >= 0) close(source_fd);
    if (uinput_fd >= 0) close(uinput_fd);
    return INPUT_PROXY_SUCCESS;
}
