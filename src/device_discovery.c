#define _XOPEN_SOURCE 700

#include "device_discovery_internal.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compare_event_nodes(
    const struct dirent **left,
    const struct dirent **right)
{
    const unsigned long left_index = strtoul((*left)->d_name + 5, NULL, 10);
    const unsigned long right_index = strtoul((*right)->d_name + 5, NULL, 10);

    if (left_index < right_index) {
        return -1;
    }
    if (left_index > right_index) {
        return 1;
    }
    return strcmp((*left)->d_name, (*right)->d_name);
}

static bool is_event_node_name(const char *name)
{
    size_t index;

    if (strncmp(name, "event", 5) != 0 || name[5] == '\0') {
        return false;
    }

    for (index = 5; name[index] != '\0'; ++index) {
        if (!isdigit((unsigned char)name[index])) {
            return false;
        }
    }

    return true;
}

static int select_event_node(const struct dirent *entry)
{
    return is_event_node_name(entry->d_name) ? 1 : 0;
}

static bool read_line(
    const char *event_path,
    const char *relative_path,
    char *value,
    size_t value_size)
{
    char path[PATH_MAX];
    FILE *file;
    size_t length;

    if (snprintf(path, sizeof(path), "%s/device/%s", event_path,
                 relative_path) >= (int)sizeof(path)) {
        return false;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        return false;
    }

    if (fgets(value, (int)value_size, file) == NULL) {
        fclose(file);
        return false;
    }
    fclose(file);

    length = strlen(value);
    while (length > 0 && (value[length - 1] == '\n' ||
                          value[length - 1] == '\r')) {
        value[--length] = '\0';
    }
    return true;
}

static bool bitmap_has_bit(const char *bitmap, unsigned int bit)
{
    char copy[1024];
    char *words[32];
    char *save_pointer = NULL;
    char *word;
    size_t word_count = 0;
    const unsigned int bits_per_word = sizeof(unsigned long) * 8U;
    size_t word_from_right;
    unsigned int word_bit;
    char *end_pointer;
    unsigned long value;

    if (strlen(bitmap) >= sizeof(copy)) {
        return false;
    }
    strcpy(copy, bitmap);

    word = strtok_r(copy, " \t", &save_pointer);
    while (word != NULL && word_count < sizeof(words) / sizeof(words[0])) {
        words[word_count++] = word;
        word = strtok_r(NULL, " \t", &save_pointer);
    }

    word_from_right = bit / bits_per_word;
    word_bit = bit % bits_per_word;
    if (word_from_right >= word_count) {
        return false;
    }

    errno = 0;
    value = strtoul(words[word_count - word_from_right - 1], &end_pointer, 16);
    if (errno != 0 || *end_pointer != '\0') {
        return false;
    }
    return (value & (1UL << word_bit)) != 0;
}

static bool capability_has_bit(
    const char *event_path,
    const char *capability,
    unsigned int bit)
{
    char bitmap[1024];

    return read_line(event_path, capability, bitmap, sizeof(bitmap)) &&
           bitmap_has_bit(bitmap, bit);
}

static bool bitmap_has_any_bit(const char *bitmap)
{
    unsigned int bit;

    for (bit = 0; bit <= KEY_MAX; ++bit) {
        if (bitmap_has_bit(bitmap, bit)) {
            return true;
        }
    }
    return false;
}

static bool bitmap_has_only_system_buttons(const char *bitmap)
{
    unsigned int bit;
    bool has_system_button = false;

    for (bit = 0; bit <= KEY_MAX; ++bit) {
        if (!bitmap_has_bit(bitmap, bit)) {
            continue;
        }
        if (bit != KEY_POWER && bit != KEY_SLEEP && bit != KEY_WAKEUP) {
            return false;
        }
        has_system_button = true;
    }
    return has_system_button;
}

static const char *classify_device(const char *event_path)
{
    const bool touch = capability_has_bit(
        event_path, "capabilities/key", BTN_TOUCH);
    const bool absolute_position =
        (capability_has_bit(event_path, "capabilities/abs", ABS_X) &&
         capability_has_bit(event_path, "capabilities/abs", ABS_Y)) ||
        (capability_has_bit(event_path, "capabilities/abs", ABS_MT_POSITION_X) &&
         capability_has_bit(event_path, "capabilities/abs", ABS_MT_POSITION_Y));
    const bool direct = capability_has_bit(
        event_path, "properties", INPUT_PROP_DIRECT);
    const bool pointer = capability_has_bit(
        event_path, "properties", INPUT_PROP_POINTER);

    if (capability_has_bit(event_path, "capabilities/key", BTN_TOOL_PEN) ||
        capability_has_bit(event_path, "capabilities/key", BTN_STYLUS)) {
        return "Tablet";
    }
    if (direct && touch && absolute_position) {
        return "Touchscreen";
    }
    if (pointer && touch && absolute_position) {
        return "Touchpad";
    }
    if (capability_has_bit(event_path, "capabilities/key", KEY_A) &&
        capability_has_bit(event_path, "capabilities/key", KEY_Z) &&
        capability_has_bit(event_path, "capabilities/key", KEY_ENTER)) {
        return "Keyboard";
    }
    if (capability_has_bit(event_path, "capabilities/rel", REL_X) &&
        capability_has_bit(event_path, "capabilities/rel", REL_Y) &&
        capability_has_bit(event_path, "capabilities/key", BTN_MOUSE)) {
        return "Mouse";
    }
    return "Other input";
}

static bool is_virtual_input(const char *event_path)
{
    char canonical_path[PATH_MAX];

    if (realpath(event_path, canonical_path) == NULL) {
        return false;
    }
    return strstr(canonical_path, "/devices/virtual/input/") != NULL;
}

static bool read_bus_type(const char *event_path, unsigned long *bus_type)
{
    char bus[16];
    char *end_pointer;

    if (!read_line(event_path, "id/bustype", bus, sizeof(bus))) {
        return false;
    }
    errno = 0;
    *bus_type = strtoul(bus, &end_pointer, 16);
    return errno == 0 && *end_pointer == '\0';
}

static const char *bus_name(unsigned long bus_type)
{
    switch (bus_type) {
        case BUS_PCI: return "PCI";
        case BUS_ISAPNP: return "ISA PnP";
        case BUS_USB: return "USB";
        case BUS_HIL: return "HIL";
        case BUS_BLUETOOTH: return "Bluetooth";
        case BUS_VIRTUAL: return "Virtual";
        case BUS_ISA: return "ISA";
        case BUS_I8042: return "i8042";
        case BUS_XTKBD: return "XT";
        case BUS_RS232: return "RS-232";
        case BUS_GAMEPORT: return "Gameport";
        case BUS_PARPORT: return "Parallel";
        case BUS_AMIGA: return "Amiga";
        case BUS_ADB: return "ADB";
        case BUS_I2C: return "I2C";
        case BUS_HOST: return "Host";
        case BUS_GSC: return "GSC";
        case BUS_ATARI: return "Atari";
        case BUS_SPI: return "SPI";
        case BUS_RMI: return "RMI";
        case BUS_CEC: return "CEC";
        case BUS_INTEL_ISHTP: return "Intel ISHTP";
        case BUS_AMD_SFH: return "AMD SFH";
        case BUS_SDW: return "SoundWire";
        default: return "Unknown";
    }
}

static bool is_plausible_source(const char *event_path)
{
    char key_bitmap[1024];
    char absolute_bitmap[1024];
    char relative_bitmap[1024];
    char switch_bitmap[1024];

    if (!read_line(event_path, "capabilities/key", key_bitmap,
                   sizeof(key_bitmap)) ||
        !read_line(event_path, "capabilities/abs", absolute_bitmap,
                   sizeof(absolute_bitmap)) ||
        !read_line(event_path, "capabilities/rel", relative_bitmap,
                   sizeof(relative_bitmap)) ||
        !read_line(event_path, "capabilities/sw", switch_bitmap,
                   sizeof(switch_bitmap))) {
        return true;
    }

    if (bitmap_has_any_bit(absolute_bitmap) ||
        bitmap_has_any_bit(relative_bitmap)) {
        return true;
    }
    if (bitmap_has_any_bit(key_bitmap)) {
        return !bitmap_has_only_system_buttons(key_bitmap);
    }

    /* Switch-only nodes and nodes without input controls are not candidates. */
    return false;
}

bool input_proxy_read_device_identity(
    const char *event_path,
    struct input_proxy_device_identity *identity)
{
    if (event_path == NULL || identity == NULL) {
        return false;
    }
    if (!read_line(event_path, "name", identity->name,
                   sizeof(identity->name)) || identity->name[0] == '\0') {
        snprintf(identity->name, sizeof(identity->name), "(unknown)");
    }
    {
        unsigned long bus_type;
        if (read_line(event_path, "id/bustype", identity->bus_id,
                      sizeof(identity->bus_id)) &&
            read_bus_type(event_path, &bus_type)) {
            identity->bus = bus_name(bus_type);
        } else {
            identity->bus_id[0] = '\0';
            identity->bus = "Unknown";
        }
    }
    if (!read_line(event_path, "id/vendor", identity->vendor_id,
                   sizeof(identity->vendor_id)))
        identity->vendor_id[0] = '\0';
    if (!read_line(event_path, "id/product", identity->product_id,
                   sizeof(identity->product_id)))
        identity->product_id[0] = '\0';
    identity->classification = classify_device(event_path);
    identity->virtual_device = is_virtual_input(event_path);
    return true;
}

enum input_proxy_result input_proxy_list_devices(
    FILE *stream,
    const char *sysfs_input_path,
    const char *device_path)
{
    struct dirent **entries = NULL;
    int entry_count;
    int index;

    if (stream == NULL || sysfs_input_path == NULL || device_path == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    entry_count = scandir(
        sysfs_input_path, &entries, select_event_node, compare_event_nodes);
    if (entry_count < 0) {
        return INPUT_PROXY_ERROR_INTERNAL;
    }

    fprintf(stream, "%-18s %-12s %-12s %s\n",
            "DEVICE", "TYPE", "BUS", "NAME");

    for (index = 0; index < entry_count; ++index) {
        char event_path[PATH_MAX];
        char devnode[PATH_MAX];
        struct input_proxy_device_identity metadata;

        if (snprintf(event_path, sizeof(event_path), "%s/%s", sysfs_input_path,
                     entries[index]->d_name) < (int)sizeof(event_path) &&
            snprintf(devnode, sizeof(devnode), "%s/%s", device_path,
                     entries[index]->d_name) < (int)sizeof(devnode) &&
            !is_virtual_input(event_path) && is_plausible_source(event_path)) {
            input_proxy_read_device_identity(event_path, &metadata);
            fprintf(stream, "%-18s %-12s %-12s %s\n",
                    devnode, metadata.classification, metadata.bus,
                    metadata.name);
        }
        free(entries[index]);
    }
    free(entries);

    fputc('\n', stream);

    return INPUT_PROXY_SUCCESS;
}
