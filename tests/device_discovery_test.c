#define _XOPEN_SOURCE 700

#include "device_discovery_internal.h"

#include <ftw.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int remove_entry(
    const char *path,
    const struct stat *status,
    int type,
    struct FTW *walk)
{
    (void)status;
    (void)type;
    (void)walk;
    return remove(path);
}

static int make_directory(const char *path)
{
    return mkdir(path, 0700) == 0 ? 0 : 1;
}

static int write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");

    if (file == NULL) {
        return 1;
    }
    if (fputs(text, file) == EOF || fclose(file) != 0) {
        return 1;
    }
    return 0;
}

static int write_bitmap(
    const char *path,
    const unsigned int *bits,
    size_t bit_count)
{
    unsigned long words[12] = {0};
    const size_t bits_per_word = sizeof(words[0]) * 8U;
    size_t highest_word = 0;
    size_t index;
    FILE *file;

    for (index = 0; index < bit_count; ++index) {
        const size_t word = bits[index] / bits_per_word;
        if (word >= sizeof(words) / sizeof(words[0])) {
            return 1;
        }
        words[word] |= 1UL << (bits[index] % bits_per_word);
        if (word > highest_word) {
            highest_word = word;
        }
    }

    file = fopen(path, "w");
    if (file == NULL) {
        return 1;
    }
    for (index = highest_word + 1; index > 0; --index) {
        const int print_result = index == highest_word + 1
            ? fprintf(file, "%lx", words[index - 1])
            : fprintf(file, " %0*lx", (int)(sizeof(unsigned long) * 2),
                      words[index - 1]);
        if (print_result < 0) {
            fclose(file);
            return 1;
        }
    }
    if (fputc('\n', file) == EOF || fclose(file) != 0) {
        return 1;
    }
    return 0;
}

static int create_device(
    const char *event_path,
    const char *name,
    const unsigned int *key_bits,
    size_t key_count,
    const unsigned int *abs_bits,
    size_t abs_count,
    const unsigned int *rel_bits,
    size_t rel_count,
    const unsigned int *property_bits,
    size_t property_count)
{
    char path[1024];

    if (make_directory(event_path) != 0) {
        return 1;
    }
    snprintf(path, sizeof(path), "%s/device", event_path);
    if (make_directory(path) != 0) {
        return 1;
    }
    snprintf(path, sizeof(path), "%s/device/capabilities", event_path);
    if (make_directory(path) != 0) {
        return 1;
    }
    snprintf(path, sizeof(path), "%s/device/id", event_path);
    if (make_directory(path) != 0) {
        return 1;
    }

#define WRITE_DEVICE_FILE(relative, text) \
    snprintf(path, sizeof(path), "%s/device/%s", event_path, relative); \
    if (write_text(path, text) != 0) return 1
#define WRITE_BITMAP_FILE(relative, bits, count) \
    snprintf(path, sizeof(path), "%s/device/%s", event_path, relative); \
    if (write_bitmap(path, bits, count) != 0) return 1

    WRITE_DEVICE_FILE("name", name);
    WRITE_DEVICE_FILE("id/bustype", "0003\n");
    WRITE_DEVICE_FILE("id/vendor", "1234\n");
    WRITE_DEVICE_FILE("id/product", "5678\n");
    WRITE_BITMAP_FILE("capabilities/key", key_bits, key_count);
    WRITE_BITMAP_FILE("capabilities/abs", abs_bits, abs_count);
    WRITE_BITMAP_FILE("capabilities/rel", rel_bits, rel_count);
    WRITE_BITMAP_FILE("capabilities/sw", NULL, 0);
    WRITE_BITMAP_FILE("properties", property_bits, property_count);

#undef WRITE_DEVICE_FILE
#undef WRITE_BITMAP_FILE
    return 0;
}

static int output_contains_in_order(
    const char *output,
    const char *first,
    const char *second)
{
    const char *first_position = strstr(output, first);
    const char *second_position = strstr(output, second);

    return first_position != NULL && second_position != NULL &&
           first_position < second_position;
}

int main(void)
{
    char temporary_root[] = "/tmp/input-proxy-discovery-test-XXXXXX";
    char *root = mkdtemp(temporary_root);
    char class_path[256];
    char path[1024];
    char virtual_event_path[256];
    char *output = NULL;
    size_t output_size = 0;
    FILE *stream;
    enum input_proxy_result result;
    int failures = 0;
    const unsigned int touchscreen_keys[] = {BTN_TOUCH};
    const unsigned int touchscreen_axes[] = {ABS_MT_POSITION_X,
                                              ABS_MT_POSITION_Y};
    const unsigned int touchscreen_properties[] = {INPUT_PROP_DIRECT};
    const unsigned int keyboard_keys[] = {KEY_A, KEY_Z, KEY_ENTER};
    const unsigned int mouse_keys[] = {BTN_MOUSE};
    const unsigned int mouse_axes[] = {REL_X, REL_Y};
    const unsigned int touchpad_properties[] = {INPUT_PROP_POINTER};
    const unsigned int tablet_keys[] = {BTN_TOOL_PEN, BTN_STYLUS};
    const unsigned int system_keys[] = {KEY_POWER};
    const unsigned int switch_bits[] = {SW_HEADPHONE_INSERT};
    const unsigned int unusual_keys[] = {KEY_F13};

    if (root == NULL) {
        return 1;
    }
    snprintf(path, sizeof(path), "%s/class", root);
    failures += make_directory(path);
    snprintf(class_path, sizeof(class_path), "%s/class/input", root);
    failures += make_directory(class_path);

    snprintf(path, sizeof(path), "%s/event10", class_path);
    failures += create_device(path, "Test Touchscreen\n",
                              touchscreen_keys, 1, touchscreen_axes, 2,
                              NULL, 0, touchscreen_properties, 1);
    snprintf(path, sizeof(path), "%s/event10/device/id/bustype", class_path);
    failures += write_text(path, "0018\n");
    snprintf(path, sizeof(path), "%s/event2", class_path);
    failures += create_device(path, "Test Keyboard\n", keyboard_keys, 3,
                              NULL, 0, NULL, 0, NULL, 0);
    snprintf(path, sizeof(path), "%s/event3", class_path);
    failures += create_device(path, "Test Mouse\n", mouse_keys, 1,
                              NULL, 0, mouse_axes, 2, NULL, 0);
    snprintf(path, sizeof(path), "%s/event4", class_path);
    failures += create_device(path, "Test Touchpad\n", touchscreen_keys, 1,
                              touchscreen_axes, 2, NULL, 0,
                              touchpad_properties, 1);
    snprintf(path, sizeof(path), "%s/event5", class_path);
    failures += create_device(path, "Test Tablet\n", tablet_keys, 2,
                              NULL, 0, NULL, 0, NULL, 0);
    snprintf(path, sizeof(path), "%s/event6", class_path);
    failures += create_device(path, "Incomplete Device\n", NULL, 0,
                              NULL, 0, NULL, 0, NULL, 0);
    snprintf(path, sizeof(path), "%s/event6/device/capabilities/key", class_path);
    if (unlink(path) != 0) {
        failures++;
    }
    snprintf(path, sizeof(path), "%s/event0", class_path);
    failures += create_device(path, "System Button\n", system_keys, 1,
                              NULL, 0, NULL, 0, NULL, 0);
    snprintf(path, sizeof(path), "%s/event1", class_path);
    failures += create_device(path, "Switch Only\n", NULL, 0,
                              NULL, 0, NULL, 0, NULL, 0);
    snprintf(path, sizeof(path), "%s/event1/device/capabilities/sw", class_path);
    failures += write_bitmap(path, switch_bits, 1);
    snprintf(path, sizeof(path), "%s/event8", class_path);
    failures += create_device(path, "CEC Remote\n", keyboard_keys, 3,
                              NULL, 0, NULL, 0, NULL, 0);
    snprintf(path, sizeof(path), "%s/event8/device/id/bustype", class_path);
    failures += write_text(path, "001e\n");
    snprintf(path, sizeof(path), "%s/event12", class_path);
    failures += create_device(path, "CEC Noise\n", NULL, 0,
                              NULL, 0, NULL, 0, NULL, 0);
    snprintf(path, sizeof(path), "%s/event12/device/id/bustype", class_path);
    failures += write_text(path, "001e\n");
    snprintf(path, sizeof(path), "%s/event11", class_path);
    failures += create_device(path, "Unusual Device\n", unusual_keys, 1,
                              NULL, 0, NULL, 0, NULL, 0);
    snprintf(path, sizeof(path), "%s/event11/device/id/bustype", class_path);
    failures += write_text(path, "ffff\n");

    snprintf(path, sizeof(path), "%s/devices", root);
    failures += make_directory(path);
    snprintf(path, sizeof(path), "%s/devices/virtual", root);
    failures += make_directory(path);
    snprintf(path, sizeof(path), "%s/devices/virtual/input", root);
    failures += make_directory(path);
    snprintf(path, sizeof(path), "%s/devices/virtual/input/input9", root);
    failures += make_directory(path);
    snprintf(virtual_event_path, sizeof(virtual_event_path),
             "%s/devices/virtual/input/input9/event9", root);
    failures += create_device(virtual_event_path, "Virtual Device\n",
                              keyboard_keys, 3, NULL, 0, NULL, 0, NULL, 0);
    snprintf(path, sizeof(path), "%s/event9", class_path);
    if (symlink(virtual_event_path, path) != 0) {
        failures++;
    }

    stream = open_memstream(&output, &output_size);
    if (failures != 0 || stream == NULL) {
        return 1;
    }
    result = input_proxy_list_devices(stream, class_path, "/dev/input");
    fclose(stream);

    if (result != INPUT_PROXY_SUCCESS ||
        !strstr(output, "DEVICE") ||
        !strstr(output, "TYPE") ||
        !strstr(output, "BUS") ||
        strstr(output, "VENDOR") ||
        !strstr(output, "/dev/input/event10") ||
        !strstr(output, "Touchscreen") ||
        !strstr(output, "I2C") ||
        !strstr(output, "Test Touchscreen") ||
        !strstr(output, "/dev/input/event2") ||
        !strstr(output, "Keyboard") ||
        !strstr(output, "Mouse") ||
        !strstr(output, "Touchpad") ||
        !strstr(output, "Tablet") ||
        !strstr(output, "USB") ||
        !strstr(output, "Incomplete Device") ||
        !strstr(output, "Other input") ||
        !strstr(output, "Unusual Device") ||
        !strstr(output, "Unknown") ||
        !strstr(output, "CEC Remote") ||
        !strstr(output, "CEC") ||
        strstr(output, "System Button") ||
        strstr(output, "Switch Only") ||
        strstr(output, "CEC Noise") ||
        strstr(output, "Virtual Device") ||
        !output_contains_in_order(output, "event2", "event10")) {
        fprintf(stderr, "unexpected discovery output:\n%s", output);
        failures++;
    }

    free(output);
    if (nftw(root, remove_entry, 16, FTW_DEPTH | FTW_PHYS) != 0) {
        failures++;
    }
    return failures == 0 ? 0 : 1;
}
