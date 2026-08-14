#ifndef INPUT_PROXY_INSTANCE_NAME_INTERNAL_H
#define INPUT_PROXY_INSTANCE_NAME_INTERNAL_H

#include <input_proxy/result.h>

struct input_proxy_instance_name;

enum input_proxy_result input_proxy_instance_name_validate(const char *name);

enum input_proxy_result input_proxy_instance_name_acquire(
    struct input_proxy_instance_name **ownership,
    const char *name
);

void input_proxy_instance_name_release(
    struct input_proxy_instance_name *ownership
);

#endif
