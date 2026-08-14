#define _POSIX_C_SOURCE 200809L

#include <input_proxy/result.h>

#include "instance_name_internal.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int expect_result(
    const char *test_name,
    enum input_proxy_result actual,
    enum input_proxy_result expected)
{
    if (actual == expected) {
        return 0;
    }

    fprintf(
        stderr,
        "%s: expected %s, got %s\n",
        test_name,
        input_proxy_result_string(expected),
        input_proxy_result_string(actual)
    );
    return 1;
}

int main(void)
{
    struct input_proxy_instance_name *first = NULL;
    struct input_proxy_instance_name *second = NULL;
    int failures = 0;
    int ready_pipe[2];
    int status;
    pid_t child;
    char ready;
    char maximum_name[80];
    char oversized_name[81];
    const char *valid_names[] = {
        "touchscreen",
        "Touchscreen",
        "_touchscreen",
        "Touchscreen_1",
        "Touch-Screen-1"
    };
    const char *invalid_names[] = {
        "",
        "1touchscreen",
        "-touchscreen",
        "touch screen",
        " touchscreen",
        "touchscreen ",
        "touch.screen",
        "touch/screen",
        "touch:screen",
        "touchscreen!",
        "touchscreen\x80"
    };
    size_t index;

    memset(maximum_name, 'a', sizeof(maximum_name) - 1);
    maximum_name[sizeof(maximum_name) - 1] = '\0';
    memset(oversized_name, 'a', sizeof(oversized_name) - 1);
    oversized_name[sizeof(oversized_name) - 1] = '\0';

    for (index = 0; index < sizeof(valid_names) / sizeof(valid_names[0]); ++index) {
        failures += expect_result(
            valid_names[index],
            input_proxy_instance_name_validate(valid_names[index]),
            INPUT_PROXY_SUCCESS
        );
    }
    failures += expect_result(
        "79-byte name",
        input_proxy_instance_name_validate(maximum_name),
        INPUT_PROXY_SUCCESS
    );

    for (index = 0;
         index < sizeof(invalid_names) / sizeof(invalid_names[0]);
         ++index) {
        failures += expect_result(
            invalid_names[index],
            input_proxy_instance_name_validate(invalid_names[index]),
            INPUT_PROXY_ERROR_INVALID_INSTANCE_NAME
        );
    }
    failures += expect_result(
        "80-byte name",
        input_proxy_instance_name_validate(oversized_name),
        INPUT_PROXY_ERROR_INVALID_INSTANCE_NAME
    );

    failures += expect_result(
        "initial ownership",
        input_proxy_instance_name_acquire(&first, "Touchscreen_Proxy"),
        INPUT_PROXY_SUCCESS
    );
    failures += expect_result(
        "duplicate ownership",
        input_proxy_instance_name_acquire(&second, "Touchscreen_Proxy"),
        INPUT_PROXY_ERROR_INSTANCE_NAME_OWNED
    );
    failures += expect_result(
        "case-sensitive name",
        input_proxy_instance_name_acquire(&second, "touchscreen_Proxy"),
        INPUT_PROXY_SUCCESS
    );
    input_proxy_instance_name_release(second);
    second = NULL;
    input_proxy_instance_name_release(first);
    first = NULL;

    failures += expect_result(
        "reuse after release",
        input_proxy_instance_name_acquire(&first, "Touchscreen_Proxy"),
        INPUT_PROXY_SUCCESS
    );
    input_proxy_instance_name_release(first);
    first = NULL;

    if (pipe(ready_pipe) != 0) {
        perror("pipe");
        return failures + 1;
    }

    child = fork();
    if (child < 0) {
        perror("fork");
        return failures + 1;
    }
    if (child == 0) {
        enum input_proxy_result result = input_proxy_instance_name_acquire(
            &first,
            "concurrent_startup"
        );

        close(ready_pipe[0]);
        if (write(ready_pipe[1], "r", 1) != 1) {
            _exit(1);
        }
        close(ready_pipe[1]);
        if (result != INPUT_PROXY_SUCCESS) {
            _exit(1);
        }
        pause();
        _exit(1);
    }

    close(ready_pipe[1]);
    if (read(ready_pipe[0], &ready, 1) != 1) {
        fprintf(stderr, "concurrent owner did not signal readiness\n");
        failures++;
    }
    close(ready_pipe[0]);
    failures += expect_result(
        "concurrent collision",
        input_proxy_instance_name_acquire(&second, "concurrent_startup"),
        INPUT_PROXY_ERROR_INSTANCE_NAME_OWNED
    );
    if (kill(child, SIGKILL) != 0 || waitpid(child, &status, 0) != child ||
        !WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL) {
        fprintf(stderr, "concurrent owner was not force-terminated\n");
        failures++;
    }
    failures += expect_result(
        "reuse after process exit",
        input_proxy_instance_name_acquire(&second, "concurrent_startup"),
        INPUT_PROXY_SUCCESS
    );
    input_proxy_instance_name_release(second);

    return failures == 0 ? 0 : 1;
}
