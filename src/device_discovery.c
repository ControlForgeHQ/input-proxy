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

struct device_metadata {
    char name[256];
    char identity[32];
    const char *classification;
};

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
        return "tablet";
    }
    if (direct && touch && absolute_position) {
        return "touchscreen";
    }
    if (pointer && touch && absolute_position) {
        return "touchpad";
    }
    if (capability_has_bit(event_path, "capabilities/key", KEY_A) &&
        capability_has_bit(event_path, "capabilities/key", KEY_Z) &&
        capability_has_bit(event_path, "capabilities/key", KEY_ENTER)) {
        return "keyboard";
    }
    if (capability_has_bit(event_path, "capabilities/rel", REL_X) &&
        capability_has_bit(event_path, "capabilities/rel", REL_Y) &&
        capability_has_bit(event_path, "capabilities/key", BTN_MOUSE)) {
        return "mouse";
    }
    return "other input";
}

static bool is_virtual_input(const char *event_path)
{
    char canonical_path[PATH_MAX];

    if (realpath(event_path, canonical_path) == NULL) {
        return false;
    }
    return strstr(canonical_path, "/devices/virtual/input/") != NULL;
}

static void read_identity(
    const char *event_path,
    char *identity,
    size_t identity_size)
{
    char bus[16];
    char vendor[16];
    char product[16];

    if (read_line(event_path, "id/bustype", bus, sizeof(bus)) &&
        read_line(event_path, "id/vendor", vendor, sizeof(vendor)) &&
        read_line(event_path, "id/product", product, sizeof(product))) {
        snprintf(identity, identity_size, "%s:%s:%s", bus, vendor, product);
    } else {
        snprintf(identity, identity_size, "-");
    }
}

static void read_metadata(
    const char *event_path,
    struct device_metadata *metadata)
{
    if (!read_line(event_path, "name", metadata->name,
                   sizeof(metadata->name)) || metadata->name[0] == '\0') {
        snprintf(metadata->name, sizeof(metadata->name), "(unknown)");
    }
    read_identity(event_path, metadata->identity, sizeof(metadata->identity));
    metadata->classification = classify_device(event_path);
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

    entry_count = scandir(sysfs_input_path, &entries, NULL, alphasort);
    if (entry_count < 0) {
        return INPUT_PROXY_ERROR_INTERNAL;
    }

    fprintf(stream, "%-18s %-12s %-14s %s\n",
            "DEVICE", "TYPE", "BUS:VENDOR:PRODUCT", "NAME");

    for (index = 0; index < entry_count; ++index) {
        char event_path[PATH_MAX];
        char devnode[PATH_MAX];
        struct device_metadata metadata;

        if (is_event_node_name(entries[index]->d_name) &&
            snprintf(event_path, sizeof(event_path), "%s/%s", sysfs_input_path,
                     entries[index]->d_name) < (int)sizeof(event_path) &&
            snprintf(devnode, sizeof(devnode), "%s/%s", device_path,
                     entries[index]->d_name) < (int)sizeof(devnode) &&
            !is_virtual_input(event_path)) {
            read_metadata(event_path, &metadata);
            fprintf(stream, "%-18s %-12s %-14s %s\n",
                    devnode,
                    metadata.classification, metadata.identity, metadata.name);
        }
        free(entries[index]);
    }
    free(entries);

    return INPUT_PROXY_SUCCESS;
}
