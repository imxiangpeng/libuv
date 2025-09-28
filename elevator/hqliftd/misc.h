#ifndef MISC_H
#define MISC_H
#include <sys/types.h>
typedef void (*misc_child_action)(pid_t pid, int status);

// called in signal action, do not block
void misc_child_exited(pid_t pid, int status);

int misc_register_child_action(misc_child_action action);
int misc_unregister_child_action(misc_child_action action);

#endif