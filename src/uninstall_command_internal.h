#ifndef INPUT_PROXY_UNINSTALL_COMMAND_INTERNAL_H
#define INPUT_PROXY_UNINSTALL_COMMAND_INTERNAL_H

#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>

enum input_proxy_uninstall_stage_result {
    INPUT_PROXY_UNINSTALL_STAGE_SUCCESS = 0,
    INPUT_PROXY_UNINSTALL_STAGE_NOT_REQUIRED,
    INPUT_PROXY_UNINSTALL_STAGE_FAILED
};

struct input_proxy_uninstall_operations {
    enum input_proxy_uninstall_stage_result (*stop_service)(
        const char *unit, void *userdata);
    enum input_proxy_uninstall_stage_result (*disable_service)(
        const char *unit, void *userdata);
    enum input_proxy_uninstall_stage_result (*remove_file)(
        const char *path, void *userdata);
    bool (*source_present)(const char *source_path, void *userdata);
    bool (*reload_udev)(void *userdata);
    bool (*trigger_source)(const char *source_path, void *userdata);
    bool (*settle_udev)(void *userdata);
    void *userdata;
};

struct input_proxy_uninstall_command_environment {
    uid_t effective_uid;
    bool interactive;
    FILE *input;
    FILE *output;
    FILE *error;
    const char *installed_instance_directory;
    const char *udev_rule_directory;
    const struct input_proxy_uninstall_operations *operations;
};

void input_proxy_uninstall_print_help(FILE *stream);
int input_proxy_uninstall_command(int argc, char *argv[]);
int input_proxy_uninstall_command_with_environment(int argc, char *argv[],
    const struct input_proxy_uninstall_command_environment *environment);

#endif
