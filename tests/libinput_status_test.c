#define _XOPEN_SOURCE 700

#include "libinput_status_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

static int write_record(const char *path, const char *contents)
{
    FILE *file = fopen(path, "w");
    if (file == NULL) return 1;
    if (fputs(contents, file) == EOF) {
        fclose(file);
        return 1;
    }
    return fclose(file) != 0;
}

int main(void)
{
    char root[] = "/tmp/input-proxy-libinput-status-test-XXXXXX";
    char record[512];
    struct stat status = {.st_mode = S_IFCHR, .st_rdev = makedev(13, 72)};
    int failures = 0;

    if (mkdtemp(root) == NULL) return 1;
    snprintf(record, sizeof(record), "%s/c13:72", root);

    if (input_proxy_read_libinput_status(root, &status, NULL, NULL) !=
        INPUT_PROXY_LIBINPUT_STATUS_INDETERMINATE) {
        fprintf(stderr, "missing udev record was not indeterminate\n");
        failures++;
    }
    failures += write_record(record, "E:ID_INPUT=1\nE:LIBINPUT_IGNORE_DEVICE=0\n");
    if (input_proxy_read_libinput_status(root, &status, NULL, NULL) !=
        INPUT_PROXY_LIBINPUT_STATUS_NOT_IGNORED) {
        fprintf(stderr, "readable non-ignored record was misclassified\n");
        failures++;
    }
    failures += write_record(record, "E:LIBINPUT_IGNORE_DEVICE=yes\n"
        "E:LIBINPUT_IGNORE_DEVICE=1\n");
    if (input_proxy_read_libinput_status(root, &status, NULL, NULL) !=
        INPUT_PROXY_LIBINPUT_STATUS_IGNORED) {
        fprintf(stderr, "ignored record was misclassified\n");
        failures++;
    }

    if (unlink(record) != 0 || rmdir(root) != 0) failures++;
    return failures == 0 ? 0 : 1;
}
