#define _XOPEN_SOURCE 700

#include "device_inspection_internal.h"
#include "deployment_readiness_internal.h"
#include "runtime_discovery_internal.h"

#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int remove_entry(const char *path, const struct stat *status, int type,
                        struct FTW *walk)
{
    (void)status; (void)type; (void)walk;
    return remove(path);
}

static int make_directory(const char *path) { return mkdir(path, 0700) != 0; }

static int write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    if (file == NULL) return 1;
    return fputs(text, file) == EOF || fclose(file) != 0;
}

int main(void)
{
    char root_template[] = "/tmp/input-proxy-inspection-test-XXXXXX";
    char *root = mkdtemp(root_template);
    char sysfs[512], event[512], path[1024], udev[512];
    char by_id_path[1024], by_path_path[1024];
    char *output = NULL, *error = NULL;
    size_t output_size = 0, error_size = 0;
    FILE *stream, *error_stream;
    enum input_proxy_result result;
    struct input_proxy_runtime_record runtime_records[3];
    struct input_proxy_runtime_snapshot runtime_snapshot = {
        .available = true,
        .records = runtime_records,
        .record_count = 3
    };
    const struct input_proxy_runtime_snapshot unavailable_snapshot = {0};
    const struct input_proxy_runtime_snapshot empty_snapshot = {
        .available = true
    };
    int failures = 0;

    if (!input_proxy_should_suggest_run(true, true, 0) ||
        input_proxy_should_suggest_run(true, true, 1) ||
        input_proxy_should_suggest_run(true, true, 2) ||
        input_proxy_should_suggest_run(false, true, 0) ||
        input_proxy_should_suggest_run(true, false, 0)) {
        fprintf(stderr, "runtime-association run suggestion policy failed\n");
        failures++;
    }

    {
        struct input_proxy_device_rule_identity identity = {
            .udev_vendor = "1234",
            .udev_model = "5678",
            .path = "platform-usb"
        };
        char *rule = input_proxy_render_libinput_ignore_rule(&identity);
        if (rule == NULL || strstr(rule,
                "ENV{ID_VENDOR_ID}==\"1234\", ENV{ID_MODEL_ID}==\"5678\"") == NULL ||
            strstr(rule, "ENV{LIBINPUT_IGNORE_DEVICE}=\"1\"") == NULL) {
            fprintf(stderr, "udev-identity rule rendering failed\n");
            failures++;
        }
        free(rule);

        identity.udev_vendor[0] = '\0';
        identity.udev_model[0] = '\0';
        snprintf(identity.path, sizeof(identity.path), "platform-i2c");
        snprintf(identity.bus, sizeof(identity.bus), "0018");
        snprintf(identity.vendor, sizeof(identity.vendor), "0416");
        snprintf(identity.product, sizeof(identity.product), "038f");
        rule = input_proxy_render_libinput_ignore_rule(&identity);
        if (rule == NULL || strstr(rule,
                "ATTRS{id/bustype}==\"0018\", ATTRS{id/vendor}==\"0416\", "
                "ATTRS{id/product}==\"038f\"") == NULL ||
            strstr(rule, "ENV{ID_PATH}==\"platform-i2c\"") == NULL) {
            fprintf(stderr, "kernel-identity rule rendering failed\n");
            failures++;
        }
        free(rule);
    }

    {
        struct input_proxy_access_diagnostics access = {
            .current_source_ok = false,
            .uinput_exists = true,
            .current_uinput_ok = false,
            .service_identity_result = INPUT_PROXY_INSTALL_SERVICE_IDENTITY_VALID,
            .service_source_ok = true,
            .service_uinput_ok = true
        };
        stream = open_memstream(&output, &output_size);
        if (stream == NULL) return 1;
        input_proxy_print_access_diagnostics(stream, &access);
        fclose(stream);
        if (strstr(output, "Runtime accessibility diagnostics") == NULL ||
            strstr(output, "Current-user access") == NULL ||
            strstr(output, "does not imply that\n    Installed Instances are unavailable") == NULL ||
            strstr(output, "sudo ") != NULL || strstr(output, "chmod") != NULL ||
            strstr(output, "chgrp") != NULL || strstr(output, "usermod") != NULL) {
            fprintf(stderr, "unexpected access diagnostics:\n%s", output);
            failures++;
        }
        free(output); output = NULL; output_size = 0;

        access.current_source_ok = false;
        access.current_uinput_ok = true;
        stream = open_memstream(&output, &output_size);
        if (stream == NULL) return 1;
        input_proxy_print_access_diagnostics(stream, &access);
        fclose(stream);
        if (strstr(output, "current user cannot read") == NULL ||
            strstr(output, "manual input-proxy run execution") == NULL ||
            strstr(output, "sudo ") != NULL ||
            strstr(output, "\033[") != NULL) {
            fprintf(stderr, "unexpected source diagnostic guidance:\n%s", output);
            failures++;
        }
        free(output); output = NULL; output_size = 0;

        access.current_source_ok = true;
        access.uinput_exists = true;
        access.current_uinput_ok = false;
        stream = open_memstream(&output, &output_size);
        if (stream == NULL) return 1;
        input_proxy_print_access_diagnostics(stream, &access);
        fclose(stream);
        if (strstr(output, "not writable by the current user") == NULL ||
            strstr(output, "Installed Instances are unavailable") == NULL ||
            strstr(output, "Suggested") != NULL) {
            fprintf(stderr, "unexpected uinput access diagnostics:\n%s", output);
            failures++;
        }
        free(output); output = NULL; output_size = 0;

        access.current_source_ok = true;
        access.uinput_exists = false;
        access.current_uinput_ok = false;
        stream = open_memstream(&output, &output_size);
        if (stream == NULL) return 1;
        input_proxy_print_access_diagnostics(stream, &access);
        fclose(stream);
        if (strstr(output, "/dev/uinput is unavailable") == NULL ||
            strstr(output, "manual input-proxy run execution") == NULL ||
            strstr(output, "modprobe") != NULL) {
            fprintf(stderr, "unexpected missing-uinput diagnostics:\n%s", output);
            failures++;
        }
        free(output); output = NULL; output_size = 0;

        access.current_source_ok = true;
        access.uinput_exists = true;
        access.current_uinput_ok = true;
        stream = open_memstream(&output, &output_size);
        if (stream == NULL) return 1;
        input_proxy_print_access_diagnostics(stream, &access);
        fclose(stream);
        if (output_size != 0) {
            fprintf(stderr, "unexpected successful-access remediation:\n%s", output);
            failures++;
        }
        free(output); output = NULL; output_size = 0;

        {
            const enum input_proxy_install_service_identity_result unavailable[] = {
                INPUT_PROXY_INSTALL_SERVICE_USER_MISSING,
                INPUT_PROXY_INSTALL_SERVICE_GROUP_MISSING,
                INPUT_PROXY_INSTALL_SERVICE_PRIMARY_GROUP_MISMATCH,
                INPUT_PROXY_INSTALL_SERVICE_INPUT_GROUP_MISSING,
                INPUT_PROXY_INSTALL_SERVICE_INPUT_MEMBERSHIP_MISSING,
                INPUT_PROXY_INSTALL_SERVICE_IDENTITY_UNUSABLE
            };
            size_t unavailable_index;
            access.current_source_ok = true;
            access.current_uinput_ok = true;
            access.service_source_ok = false;
            access.service_uinput_ok = false;
            for (unavailable_index = 0;
                 unavailable_index < sizeof(unavailable) / sizeof(unavailable[0]);
                 ++unavailable_index) {
                access.service_identity_result = unavailable[unavailable_index];
                stream = open_memstream(&output, &output_size);
                if (stream == NULL) return 1;
                input_proxy_print_access_diagnostics(stream, &access);
                fclose(stream);
                if (strstr(output, "Service-identity access") == NULL ||
                    strstr(output, "supplementary input-group membership") == NULL ||
                    strstr(output, "Check package integration") == NULL ||
                    strstr(output, "Current-user access") != NULL) {
                    fprintf(stderr, "unexpected unavailable-service diagnostics:\n%s",
                            output);
                    failures++;
                }
                free(output); output = NULL; output_size = 0;
            }
        }

        access.service_identity_result = INPUT_PROXY_INSTALL_SERVICE_IDENTITY_VALID;
        access.service_source_ok = false;
        access.service_uinput_ok = false;
        stream = open_memstream(&output, &output_size);
        if (stream == NULL) return 1;
        input_proxy_print_access_diagnostics(stream, &access);
        fclose(stream);
        if (strstr(output, "cannot read this input device") == NULL ||
            strstr(output, "cannot open /dev/uinput for writing") == NULL ||
            strstr(output, "package-owned") == NULL) {
            fprintf(stderr, "unexpected broken-service diagnostics:\n%s", output);
            failures++;
        }
        free(output); output = NULL; output_size = 0;

        access.service_source_ok = true;
        access.service_uinput_ok = true;
    }

    {
        const gid_t supplementary_groups[] = {44, 46};
        struct input_proxy_deployment_environment service = {
            .service_uid = 1001,
            .service_gid = 1001,
            .service_groups = supplementary_groups,
            .service_group_count = 2
        };
        struct stat permission = {
            .st_uid = 0,
            .st_gid = 46,
            .st_mode = S_IFCHR | 0660
        };
        if (!input_proxy_deployment_identity_has_access(
                &permission, &service, S_IRUSR, S_IRGRP, S_IROTH) ||
            !input_proxy_deployment_identity_has_access(
                &permission, &service, S_IWUSR, S_IWGRP, S_IWOTH)) {
            fprintf(stderr, "supplementary-group access calculation failed\n");
            failures++;
        }
        permission.st_gid = 47;
        if (input_proxy_deployment_identity_has_access(
                &permission, &service, S_IRUSR, S_IRGRP, S_IROTH) ||
            input_proxy_deployment_identity_has_access(
                &permission, &service, S_IWUSR, S_IWGRP, S_IWOTH)) {
            fprintf(stderr, "unrelated-group access calculation failed\n");
            failures++;
        }
        permission.st_uid = service.service_uid;
        permission.st_mode = S_IFCHR | 0400;
        if (!input_proxy_deployment_identity_has_access(
                &permission, &service, S_IRUSR, S_IRGRP, S_IROTH) ||
            input_proxy_deployment_identity_has_access(
                &permission, &service, S_IWUSR, S_IWGRP, S_IWOTH)) {
            fprintf(stderr, "owner access calculation failed\n");
            failures++;
        }
    }

    if (root == NULL) return 1;
    snprintf(sysfs, sizeof(sysfs), "%s/sys", root);
    snprintf(event, sizeof(event), "%s/event7", sysfs);
    snprintf(udev, sizeof(udev), "%s/udev", root);
    failures += make_directory(sysfs);
    failures += make_directory(event);
    snprintf(path, sizeof(path), "%s/device", event); failures += make_directory(path);
    snprintf(path, sizeof(path), "%s/device/capabilities", event); failures += make_directory(path);
    snprintf(path, sizeof(path), "%s/device/id", event); failures += make_directory(path);
    snprintf(path, sizeof(path), "%s/device/name", event); failures += write_text(path, "Fixture Keyboard\n");
    snprintf(path, sizeof(path), "%s/device/id/bustype", event); failures += write_text(path, "0003\n");
    snprintf(path, sizeof(path), "%s/device/capabilities/key", event); failures += write_text(path, "0\n");
    snprintf(path, sizeof(path), "%s/device/capabilities/abs", event); failures += write_text(path, "0\n");
    snprintf(path, sizeof(path), "%s/device/capabilities/rel", event); failures += write_text(path, "0\n");
    snprintf(path, sizeof(path), "%s/device/properties", event); failures += write_text(path, "0\n");
    snprintf(path, sizeof(path), "%s/dev", event); failures += write_text(path, "1:3\n");
    failures += make_directory(udev);
    snprintf(path, sizeof(path), "%s/c1:3", udev);
    failures += write_text(path, "E:ID_INPUT=1\n");
    snprintf(path, sizeof(path), "%s/event7", root);
    if (symlink("/dev/null", path) != 0) failures++;
    {
        char alias_directory[1024];
        snprintf(alias_directory, sizeof(alias_directory), "%s/by-id", root);
        failures += make_directory(alias_directory);
        snprintf(by_id_path, sizeof(by_id_path), "%s/by-id/test-device", root);
        if (symlink("../event7", by_id_path) != 0) failures++;
        snprintf(alias_directory, sizeof(alias_directory), "%s/by-path", root);
        failures += make_directory(alias_directory);
        snprintf(by_path_path, sizeof(by_path_path), "%s/by-path/test-port", root);
        if (symlink("../event7", by_path_path) != 0) failures++;
    }
    runtime_records[0] = (struct input_proxy_runtime_record) {
        .instance_name = "EventSource",
        .source_path = path
    };
    runtime_records[1] = (struct input_proxy_runtime_record) {
        .instance_name = "PersistentSource",
        .source_path = by_id_path
    };
    runtime_records[2] = (struct input_proxy_runtime_record) {
        .instance_name = "Unrelated",
        .source_path = "/dev/input/unrelated"
    };

    stream = open_memstream(&output, &output_size);
    if (stream == NULL) return 1;
    input_proxy_print_runtime_associations(
        stream, &empty_snapshot, path, by_id_path);
    fclose(stream);
    if (output[0] != '\0') {
        fprintf(stderr, "no-match runtime association output was not silent\n");
        failures++;
    }
    free(output); output = NULL; output_size = 0;

    stream = open_memstream(&output, &output_size);
    error_stream = open_memstream(&error, &error_size);
    if (failures || stream == NULL || error_stream == NULL) return 1;
    result = input_proxy_inspect_device(stream, error_stream, path, sysfs, root,
                                        "/dev/null", udev, &runtime_snapshot);
    fclose(stream); fclose(error_stream);
    if (result != INPUT_PROXY_SUCCESS ||
        strstr(output, "Device identity") == NULL ||
        strstr(output, "Event node:") == NULL ||
        strstr(output, path) == NULL ||
        strstr(output, "Preferred run source:") == NULL ||
        strstr(output, "/by-id/test-device") == NULL ||
        strstr(output, "Fixture Keyboard") == NULL ||
        strstr(output, "Libinput ignored:      No") == NULL ||
        strstr(output, "Bus:                   USB") == NULL ||
        strstr(output, "Current user\n") == NULL ||
        strstr(output, "Source readable:       Yes\n") == NULL ||
        strstr(output, "Service identity\n") == NULL ||
        strstr(output, "Available:             Yes\n") == NULL ||
        strstr(output, "/dev/uinput writable:  Yes") == NULL ||
        strstr(output, "No udev rule suggested") == NULL ||
        strstr(output, "Suggested udev rule") != NULL ||
        strstr(output, "(not applied)") != NULL ||
        strstr(output, "(not run)") != NULL ||
        strstr(output, "Manual run:            READY WITH WARNINGS") == NULL ||
        strstr(output, "Installed Instance:    READY WITH WARNINGS") == NULL ||
        strstr(output, "Runtime accessibility diagnostics") != NULL ||
        strstr(output, "Libinput remediation") == NULL ||
        strstr(output, "Associated proxy instances\n") == NULL ||
        strstr(output, "Associated proxy instances:\n") != NULL ||
        strstr(output, "Running input-proxy instances") != NULL ||
        strstr(output, "  EventSource [") == NULL ||
        strstr(output, path) == NULL ||
        strstr(output, "  PersistentSource [") == NULL ||
        strstr(output, by_id_path) == NULL ||
        strstr(output, "Unrelated") != NULL ||
        strstr(output, "Proxy readiness") >
            strstr(output, "Libinput remediation") ||
        strstr(output, "Suggested manual command") != NULL ||
        strstr(output, "Suggested installation command") == NULL ||
        strstr(output, "(BLOCKER)") != NULL ||
        strstr(output, "input-proxy run \\\n") != NULL ||
        strstr(output, "\033[") != NULL || output_size < 2 ||
        strcmp(output + output_size - 2, "\n\n") != 0 ||
        (output_size >= 3 && strcmp(output + output_size - 3, "\n\n\n") == 0) ||
        error[0] != '\0') {
        fprintf(stderr, "unexpected inspection result:\n%s%s", output, error);
        failures++;
    }
    free(output); free(error);
    output = NULL; error = NULL; output_size = 0; error_size = 0;
    snprintf(path, sizeof(path), "%s/device/id/vendor", event);
    failures += write_text(path, "0416\n");
    snprintf(path, sizeof(path), "%s/device/id/product", event);
    failures += write_text(path, "038f\n");
    snprintf(path, sizeof(path), "%s/c1:3", udev);
    failures += write_text(path, "E:ID_INPUT=1\nE:ID_PATH=platform-i2c\n");
    snprintf(path, sizeof(path), "%s/event7", root);
    stream = open_memstream(&output, &output_size);
    error_stream = open_memstream(&error, &error_size);
    if (failures || stream == NULL || error_stream == NULL) return 1;
    result = input_proxy_inspect_device(stream, error_stream, path, sysfs, root,
                                        "/dev/null", udev, &empty_snapshot);
    fclose(stream); fclose(error_stream);
    if (result != INPUT_PROXY_SUCCESS ||
        strstr(output, "ATTRS{id/bustype}==\"0003\"") == NULL ||
        strstr(output, "ATTRS{id/vendor}==\"0416\"") == NULL ||
        strstr(output, "ATTRS{id/product}==\"038f\"") == NULL ||
        strstr(output, "ENV{ID_PATH}==\"platform-i2c\"") == NULL ||
        strstr(output, "ENV{LIBINPUT_IGNORE_DEVICE}=\"1\"") == NULL ||
        error[0] != '\0') {
        fprintf(stderr, "inspection did not render the kernel-identity rule:\n%s%s",
                output, error);
        failures++;
    }
    free(output); free(error);
    output = NULL; error = NULL; output_size = 0; error_size = 0;
    snprintf(path, sizeof(path), "%s/device/id/vendor", event);
    if (unlink(path) != 0) failures++;
    snprintf(path, sizeof(path), "%s/device/id/product", event);
    if (unlink(path) != 0) failures++;
    snprintf(path, sizeof(path), "%s/c1:3", udev);
    failures += write_text(path, "E:ID_INPUT=1\n");
    snprintf(path, sizeof(path), "%s/event7", root);
    stream = open_memstream(&output, &output_size);
    error_stream = open_memstream(&error, &error_size);
    if (stream == NULL || error_stream == NULL) return 1;
    result = input_proxy_inspect_device(stream, error_stream, path, sysfs, root,
                                        "/definitely/missing/uinput", udev,
                                        &unavailable_snapshot);
    fclose(stream); fclose(error_stream);
    if (result != INPUT_PROXY_SUCCESS ||
        strstr(output, "/dev/uinput exists:    No") == NULL ||
        strstr(output, "/dev/uinput writable:  Unavailable") == NULL ||
        strstr(output, "Suggested manual command") != NULL ||
        strstr(output, "Runtime instance information unavailable: system "
            "D-Bus could not be queried.") == NULL ||
        error[0] != '\0') {
        fprintf(stderr, "unexpected missing-uinput inspection result:\n%s%s",
                output, error);
        failures++;
    }
    free(output); free(error);
    {
        const char *const aliases[] = {by_id_path, by_path_path};
        size_t alias_index;
        for (alias_index = 0; alias_index < 2; ++alias_index) {
            output = NULL; error = NULL; output_size = 0; error_size = 0;
            stream = open_memstream(&output, &output_size);
            error_stream = open_memstream(&error, &error_size);
            if (stream == NULL || error_stream == NULL) return 1;
            result = input_proxy_inspect_device(stream, error_stream,
                aliases[alias_index], sysfs, root, "/dev/null", udev,
                &runtime_snapshot);
            fclose(stream); fclose(error_stream);
            if (result != INPUT_PROXY_SUCCESS ||
                strstr(output, aliases[alias_index]) == NULL ||
                strstr(output, "Event node:") == NULL ||
                strstr(output, path) == NULL ||
                strstr(output, "Associated proxy instances\n") == NULL ||
                strstr(output, "  EventSource [") == NULL ||
                strstr(output, "  PersistentSource [") == NULL ||
                strstr(output, "Unrelated") != NULL || error[0] != '\0') {
                fprintf(stderr, "unexpected alias inspection result:\n%s%s",
                        output, error);
                failures++;
            }
            free(output); free(error);
        }
    }
    output = NULL;
    output_size = 0;
    stream = open_memstream(&output, &output_size);
    if (stream == NULL) return 1;
    {
        const char *const values[] = {
            "KEY_ESC", "KEY_1", "KEY_2", "KEY_3", "KEY_4", "KEY_5",
            "KEY_6", "KEY_7", "KEY_8", "KEY_9", "KEY_0", "KEY_MINUS"
        };
        input_proxy_print_wrapped_values(stream, "Keys/buttons:", values,
                                         sizeof(values) / sizeof(values[0]));
    }
    fclose(stream);
    if (strstr(output, "\n                         KEY_") == NULL ||
        strstr(output, "\033[") != NULL) {
        fprintf(stderr, "unexpected wrapped output:\n%s", output);
        failures++;
    }
    free(output);
    output = NULL; error = NULL; output_size = 0; error_size = 0;
    snprintf(path, sizeof(path), "%s/c1:3", udev);
    failures += write_text(path, "E:LIBINPUT_IGNORE_DEVICE=1\n");
    stream = open_memstream(&output, &output_size);
    error_stream = open_memstream(&error, &error_size);
    if (stream == NULL || error_stream == NULL) return 1;
    snprintf(path, sizeof(path), "%s/event7", root);
    result = input_proxy_inspect_device(stream, error_stream, path, sysfs, root,
                                        "/dev/null", udev, &empty_snapshot);
    fclose(stream); fclose(error_stream);
    if (result != INPUT_PROXY_SUCCESS ||
        strstr(output, "Libinput ignored:      Yes") == NULL ||
        strstr(output, "Libinput remediation") != NULL || error[0] != '\0') {
        fprintf(stderr, "unexpected ignored inspection result:\n%s%s", output,
                error);
        failures++;
    }
    free(output); free(error);
    if (nftw(root, remove_entry, 16, FTW_DEPTH | FTW_PHYS) != 0) failures++;
    return failures == 0 ? 0 : 1;
}
