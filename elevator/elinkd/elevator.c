#include "elevator.h"
#include "topic_property.h"

static char _elevator_id[128] = {0};
int elevator_property_elevator_id(struct property*self, struct property_value *val) {
    (void)self;
    (void)val;
    
    property_value_set_string_ext(val, _elevator_id, 1);
    
    // self->dirty = 1;

    return 0;
}
