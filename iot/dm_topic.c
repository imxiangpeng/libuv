#include "dm_topic.h"

extern int dm_topic_heartbeat_init(void);
extern int dm_topic_command_init(void);
extern int dm_topic_liftstate_init(void);
extern int dm_topic_liftfault_init(void);
int dm_topic_init(void) {
    dm_topic_heartbeat_init();
    dm_topic_command_init();

    dm_topic_liftstate_init();

    dm_topic_liftfault_init();
    return 0;
}