#define _POSIX_C_SOURCE 200809L

#include <input_proxy/result.h>

#include "instance_name_internal.h"

#include <signal.h>
#include <stdio.h>
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

    failures += expect_result(
        "initial ownership",
        input_proxy_instance_name_acquire(&first, "Touchscreen Proxy !@#$%^&*()"),
        INPUT_PROXY_SUCCESS
    );
    failures += expect_result(
        "duplicate ownership",
        input_proxy_instance_name_acquire(&second, "Touchscreen Proxy !@#$%^&*()"),
        INPUT_PROXY_ERROR_INSTANCE_NAME_OWNED
    );
    failures += expect_result(
        "different name",
        input_proxy_instance_name_acquire(&second, "Second Touchscreen"),
        INPUT_PROXY_SUCCESS
    );
    input_proxy_instance_name_release(second);
    second = NULL;
    input_proxy_instance_name_release(first);
    first = NULL;

    failures += expect_result(
        "reuse after release",
        input_proxy_instance_name_acquire(&first, "Touchscreen Proxy !@#$%^&*()"),
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
            "concurrent startup"
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
        input_proxy_instance_name_acquire(&second, "concurrent startup"),
        INPUT_PROXY_ERROR_INSTANCE_NAME_OWNED
    );
    if (kill(child, SIGKILL) != 0 || waitpid(child, &status, 0) != child ||
        !WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL) {
        fprintf(stderr, "concurrent owner was not force-terminated\n");
        failures++;
    }
    failures += expect_result(
        "reuse after process exit",
        input_proxy_instance_name_acquire(&second, "concurrent startup"),
        INPUT_PROXY_SUCCESS
    );
    input_proxy_instance_name_release(second);

    return failures == 0 ? 0 : 1;
}
