#include "dm_topic.h"



extern struct dm_topic dm_topic_heartbeat;
extern struct dm_topic dm_topic_command;
int dm_topic_init(void) {

    dm_topic_register(&dm_topic_heartbeat);
    dm_topic_register(&dm_topic_command);
    
    return 0;
}