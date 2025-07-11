#ifndef ELEVATOR_H
#define ELEVATOR_H
#include <stdint.h>
#include "property.h"

#include "libubus.h"

struct floor_calibration_event {
    int id;
    int floor;
    char label[64];
    double height;
    double pressure;
    struct list_head entry;
};


int elevator_property_elevator_id(struct property*self);
int elevator_property_enter_sensor_calibration(struct property *self, struct property_value* value);
int elevator_property_get_hqliftd_config(struct property*self);
int elevator_property_set_hqliftd_config(struct property*self, struct property_value* value);

void elevator_ubus_event_handler(struct ubus_context* ctx,
                               struct ubus_event_handler* ev,
                               const char* type,
                               struct blob_attr* msg);

int elevator_elevatord_subscriber_callback(struct ubus_context* ctx, struct ubus_object* obj, struct ubus_request_data* req, const char* method, struct blob_attr* msg);

void elevator_elevatord_connected(struct ubus_context* ctx, uint32_t id);
void elevator_elevatord_disconnected(struct ubus_context* ctx);


int elevator_floor_enter_calibration(int floor_base, int floors_below_base, int floors_above_base);

int elevator_floor_update_floor_model_data(const char* data);
// implemented in topic_event.c
struct floor_calibration_event* floor_calibration_event_alloc();
int send_floor_calibration_event(struct floor_calibration_event*);

#endif