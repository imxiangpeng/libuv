#include "misc.h"
#include <stddef.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
static misc_child_action _child_actions[20] = {NULL};

void misc_child_exited(pid_t pid, int status) {
    for (size_t i = 0; i < ARRAY_SIZE(_child_actions); i++) {
        if (_child_actions[i]) {
            _child_actions[i](pid, status);
        }
    }
}

int misc_register_child_action(misc_child_action action) {
    for (size_t i = 0; i < ARRAY_SIZE(_child_actions); i++) {
        if (!_child_actions[i]) {
            _child_actions[i] = action;
            return 0;
        }
    }

    return -1;
}

// should use lock?
int misc_unregister_child_action(misc_child_action action) {
    for (size_t i = 0; i < ARRAY_SIZE(_child_actions); i++) {
        if (_child_actions[i] == action) {
            _child_actions[i] = NULL;
            return 0;
        }
    }

    return -1;
}
