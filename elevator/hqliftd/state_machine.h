#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

enum state_machine_state {
    SM_ELEVATOR_UNINIT = 0,
    SM_ELEVATOR_STOPPED,
    SM_ELEVATOR_STOPPED_DOOR_OPENED,
    SM_ELEVATOR_STOPPED_DOOR_CLOSED,
    SM_ELEVATOR_RUNNING
};

enum state_machine_event {
    SM_EVENT_STOPPED = 0,
    SM_EVENT_DOOR_OPENED,
    SM_EVENT_DOOR_CLOSED,
    SM_EVENT_RUNNING,
};

struct state_machine {
    enum state_machine_state state;
    void (*on_enter)();
    void (*on_exit)();
    int (*process_message)(int message);
};

struct uv_loop_s;


int statemachine_init(struct uv_loop_s *loop);

int statemachine_deinit();

int statemachine_post(enum state_machine_event message);

#endif