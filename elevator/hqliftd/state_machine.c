// mxp, 20250505, a simple "state machine"
// used to verify some state exception
// such as door not opened after stopped
// door not closed before running
// not it's verify simple !
// there is a lot of work to be done

#include "state_machine.h"

#include <stdlib.h>
#include <unistd.h>
#include <uv.h>

#include "elevator.h"
#include "hr_log.h"
#include "option.h"
#include "uelevator.h"

// see option.h
// door should opened within 5 seconds
// mxp, 20250607, door detector maybe too later
// #define DOOR_OPEN_TIMEOUT_AFTER_STOPPED 90000  // 5s
// notice when person in elevator long time
// notice when a person is detected in the elevator while it is stationary and the doors are closed
// #define SOMEONE_INSIDE_WHEN_DOOR_CLOSED_TIMEOUT 90000  // 60s

static int _pipefd[2] = {-1};

static enum state_machine_state _state = SM_ELEVATOR_UNINIT;
static uv_poll_t _state_machine_poll;
static uv_timer_t _timer;

// Fault:
// 1. 关人/困人
//    -
// 2. 开门走车
//    -
// 1. 超速
//    可以从 uelevator 中采集 RealTime 事件中的速度信息，当大于门限的时候上报
// 2.
//

static uint32_t _elevator_exception = ELEVATOR_EXCEPTION_NONE;

static const char* state_str(enum state_machine_state state) {
    switch (state) {
        case SM_ELEVATOR_UNINIT:
            return "uninit";
        case SM_ELEVATOR_STOPPED:
            return "stopped";
        case SM_ELEVATOR_STOPPED_DOOR_OPENED:
            return "door opened when stopped";
        case SM_ELEVATOR_STOPPED_DOOR_CLOSED:
            return "door closed when stopped";
        case SM_ELEVATOR_RUNNING:
            return "running";
        default:
            return "undefined";
    }
}

static const char* event_str(enum state_machine_event event) {
    switch (event) {
        case SM_EVENT_STOPPED:
            return "stopped";
        case SM_EVENT_DOOR_OPENED:
            return "door opened";
        case SM_EVENT_DOOR_CLOSED:
            return "door closed";
        case SM_EVENT_RUNNING:
            return "running";
        default:
            return "known";
    }
    return "";
}

static void _wait_door_opened_after_stopped_cb(uv_timer_t* handle) {
    struct elevator_status st;
    if (!handle)
        return;

    uelevator_get_status(&st);
    printf("%s(%d): come in door not opened after stopped...\n", __FUNCTION__, __LINE__);
    HR_LOGD("%s(%d): come in door not opened after stopped..., door:%d, passenger:%d\n", __FUNCTION__, __LINE__, st.door_state, st.passenger_count);

    if (0 == (_elevator_exception & ELEVATOR_EXCEPTION_PEOPLE_TRAPPED)) {
        if (st.passenger_count > 0) {
            _elevator_exception |= ELEVATOR_EXCEPTION_PEOPLE_TRAPPED;
            HR_LOGD("%s(%d): !!! fire event: people is in elevator while door is not opened ...\n", __FUNCTION__, __LINE__);

            // kunren maybe disabled
            if (_eguard_options[OPTION_EGUARD_KUNREN_DETECT_ENABLED].value.number == 0) {
                HR_LOGD("eguard_kunren_detect_enabled is 0!\n");
                return;
            }

            elevator_fault_occurred(ELEVATOR_EXCEPTION_PEOPLE_TRAPPED);
        }
    }
}

// 也有可能是新进入的乘客，然后电梯一直没有运行也可以报警
static void _detect_someone_inside_when_long_stopped(uv_timer_t* handle) {
    struct elevator_status st;
    if (!handle)
        return;

    uelevator_get_status(&st);

    if (0 == (_elevator_exception & ELEVATOR_EXCEPTION_PEOPLE_TRAPPED)) {
        if (st.passenger_count > 0) {
            _elevator_exception |= ELEVATOR_EXCEPTION_PEOPLE_TRAPPED;
            HR_LOGD("%s(%d): !!! fire event: people is in elevator while door is not opened ...\n", __FUNCTION__, __LINE__);

            // kunren maybe disabled
            if (_eguard_options[OPTION_EGUARD_KUNREN_DETECT_ENABLED].value.number == 0) {
                HR_LOGD("eguard_kunren_detect_enabled is 0!\n");
                return;
            }

            elevator_fault_occurred(ELEVATOR_EXCEPTION_PEOPLE_TRAPPED);
        }
    }

    printf("%s(%d): come in door closed and we are stopped but some one is still in elevator...:%d\n", __FUNCTION__, __LINE__, st.passenger_count);
    HR_LOGE("%s(%d): come in door closed and we are stopped but some one is still in elevator...:%d\n", __FUNCTION__, __LINE__, st.passenger_count);
}
static void _statemachine_message_handle(uv_poll_t* handle, int status, int events) {
    (void)handle;
    (void)status;
    (void)events;

    enum state_machine_event event = -1;

    if (!(events & UV_READABLE)) {
        return;
    }

    size_t rc = read(handle->io_watcher.fd, &event, sizeof(event));
    if (rc != sizeof(event)) {
        return;
    }

    printf("state machine message: %d(%s) received event %d(%s), exception:%d\n", _state, state_str(_state), event, event_str(event), _elevator_exception);
    HR_LOGD("state machine message: %d(%s) received event %d(%s), exception:%d\n", _state, state_str(_state), event, event_str(event), _elevator_exception);

    // finished trapped event when door opened in any case
    if (SM_EVENT_DOOR_OPENED == event) {
        if (0 != (_elevator_exception & ELEVATOR_EXCEPTION_PEOPLE_TRAPPED)) {
            _elevator_exception &= ~ELEVATOR_EXCEPTION_PEOPLE_TRAPPED;
            elevator_fault_resolved(ELEVATOR_EXCEPTION_PEOPLE_TRAPPED);
        }
    } else if (SM_EVENT_DOOR_CLOSED == event) {
        if (0 != (_elevator_exception & ELEVATOR_EXCEPTION_RUN_WITHOUT_DOOR_CLOSED)) {
            _elevator_exception &= ~ELEVATOR_EXCEPTION_RUN_WITHOUT_DOOR_CLOSED;
            elevator_fault_resolved(ELEVATOR_EXCEPTION_RUN_WITHOUT_DOOR_CLOSED);
        }
    }

    switch (_state) {
        case SM_ELEVATOR_UNINIT:
            // directly assign _state according message, ignore error
            switch (event) {
                case SM_EVENT_STOPPED:
                    _state = SM_ELEVATOR_STOPPED;
                    break;
                case SM_EVENT_RUNNING:
                    _state = SM_ELEVATOR_RUNNING;
                    break;
                case SM_EVENT_DOOR_OPENED:
                    _state = SM_ELEVATOR_STOPPED_DOOR_OPENED;
                    break;
                case SM_EVENT_DOOR_CLOSED:
                    _state = SM_ELEVATOR_STOPPED_DOOR_CLOSED;
                    break;
            }
            break;
        case SM_ELEVATOR_STOPPED:
            // expect door open -> door close -> running
            switch (event) {
                case SM_EVENT_DOOR_OPENED:
                    HR_LOGD("%s(%d): from %s ==> %s\n", __FUNCTION__, __LINE__, state_str(_state), state_str(SM_ELEVATOR_STOPPED_DOOR_OPENED));
                    _state = SM_ELEVATOR_STOPPED_DOOR_OPENED;
                    uv_timer_stop(&_timer);  // stop the door open wait timer
                    break;
                case SM_EVENT_RUNNING:
                    HR_LOGD("%s(%d): !!! door not opened! current state:%d(%s), not support event:%d(%s)\n", __FUNCTION__, __LINE__, _state, state_str(_state), event, event_str(event));
                    break;
                default:
                    printf("%s(%d): current state:%d(%s), not support event:%d(%s)\n", __FUNCTION__, __LINE__, _state, state_str(_state), event, event_str(event));
                    HR_LOGD("%s(%d): current state:%d(%s), not support event:%d(%s)\n", __FUNCTION__, __LINE__, _state, state_str(_state), event, event_str(event));
                    break;
            }
            break;
        case SM_ELEVATOR_STOPPED_DOOR_OPENED:
            switch (event) {
                case SM_EVENT_DOOR_CLOSED:
                    HR_LOGD("%s(%d): from %s ==> %s\n", __FUNCTION__, __LINE__, state_str(_state), state_str(SM_ELEVATOR_STOPPED_DOOR_CLOSED));
                    _state = SM_ELEVATOR_STOPPED_DOOR_CLOSED;
                    // check anyone is still in elevator but elevator is not running
                    // SOMEONE_INSIDE_WHEN_DOOR_CLOSED_TIMEOUT
                    uv_timer_start(&_timer, _detect_someone_inside_when_long_stopped, _eguard_options[OPTION_EGUARD_KUNREN_DETECT_TIMEOUT].value.number, 0);
                    break;
                case SM_EVENT_RUNNING:
                    if (0 == (_elevator_exception & ELEVATOR_EXCEPTION_RUN_WITHOUT_DOOR_CLOSED)) {
                        _elevator_exception |= ELEVATOR_EXCEPTION_RUN_WITHOUT_DOOR_CLOSED;
                        elevator_fault_occurred(ELEVATOR_EXCEPTION_RUN_WITHOUT_DOOR_CLOSED);
                    }
                    printf("%s(%d): Exception: door is opened when running !\n", __FUNCTION__, __LINE__);
                    HR_LOGE("%s(%d): Exception: door is opened when running !\n", __FUNCTION__, __LINE__);
                    break;
                default:
                    HR_LOGD("%s(%d): current state:%d(%s), not support event:%d(%s)\n", __FUNCTION__, __LINE__, _state, state_str(_state), event, event_str(event));
                    printf("%s(%d): current state:%d(%s), not support event:%d(%s)\n", __FUNCTION__, __LINE__, _state, state_str(_state), event, event_str(event));
                    break;
            }
            break;
        case SM_ELEVATOR_STOPPED_DOOR_CLOSED:
            switch (event) {
                case SM_EVENT_RUNNING:
                    HR_LOGD("%s(%d): from %s ==> %s\n", __FUNCTION__, __LINE__, state_str(_state), state_str(SM_ELEVATOR_RUNNING));
                    _state = SM_ELEVATOR_RUNNING;
                    uv_timer_stop(&_timer);  // stop the person inside long timer
                    break;
                case SM_EVENT_DOOR_OPENED:

                    printf("%s(%d): door closed but open again: current state:%d(%s), not support event:%d(%s)\n", __FUNCTION__, __LINE__, _state, state_str(_state), event, event_str(event));
                    HR_LOGD("%s(%d): door closed but open again: current state:%d(%s), not support event:%d(%s)\n", __FUNCTION__, __LINE__, _state, state_str(_state), event, event_str(event));
                    HR_LOGD("%s(%d): from %s ==> %s\n", __FUNCTION__, __LINE__, state_str(_state), state_str(SM_ELEVATOR_STOPPED_DOOR_OPENED));
                    _state = SM_ELEVATOR_STOPPED_DOOR_OPENED;
                    uv_timer_stop(&_timer);  // stop the person inside long timer
                    break;
                default:
                    HR_LOGD("%s(%d): current state:%d(%s), not support event:%d(%s)\n", __FUNCTION__, __LINE__, _state, state_str(_state), event, event_str(event));
                    printf("%s(%d): current state:%d(%s), not support event:%d(%s)\n", __FUNCTION__, __LINE__, _state, state_str(_state), event, event_str(event));
                    break;
            }
            break;
        case SM_ELEVATOR_RUNNING:
            switch (event) {
                case SM_EVENT_STOPPED:
                    HR_LOGD("%s(%d): from %s ==> %s\n", __FUNCTION__, __LINE__, state_str(_state), state_str(SM_ELEVATOR_STOPPED));
                    _state = SM_ELEVATOR_STOPPED;
                    // start timer to detect door open
                    // DOOR_OPEN_TIMEOUT_AFTER_STOPPED
                    uv_timer_start(&_timer, _wait_door_opened_after_stopped_cb, _eguard_options[OPTION_EGUARD_KUNREN_DETECT_TIMEOUT].value.number, 0);
                    break;
                case SM_EVENT_DOOR_OPENED:
                    printf("%s(%d): Exception door is opened while running\n", __FUNCTION__, __LINE__);
                    if (0 == (_elevator_exception & ELEVATOR_EXCEPTION_RUN_WITHOUT_DOOR_CLOSED)) {
                        _elevator_exception |= ELEVATOR_EXCEPTION_RUN_WITHOUT_DOOR_CLOSED;
                        elevator_fault_occurred(ELEVATOR_EXCEPTION_RUN_WITHOUT_DOOR_CLOSED);
                    }

                    HR_LOGD("%s(%d): !!! fire event: door opened while running ...\n", __FUNCTION__, __LINE__);
                    break;
                case SM_EVENT_DOOR_CLOSED:
                    // ignore
                    if (0 != (_elevator_exception & ELEVATOR_EXCEPTION_RUN_WITHOUT_DOOR_CLOSED)) {
                        _elevator_exception &= ~ELEVATOR_EXCEPTION_RUN_WITHOUT_DOOR_CLOSED;
                        elevator_fault_resolved(ELEVATOR_EXCEPTION_RUN_WITHOUT_DOOR_CLOSED);
                    }

                    break;
                default:
                    HR_LOGD("%s(%d): current state:%d(%s), not support event:%d(%s)\n", __FUNCTION__, __LINE__, _state, state_str(_state), event, event_str(event));
                    printf("%s(%d): current state:%d(%s), not support event:%d(%s)\n", __FUNCTION__, __LINE__, _state, state_str(_state), event, event_str(event));
                    break;
            }
            break;
        default:
            break;
    }
}
int statemachine_init(uv_loop_t* loop) {
    if (0 != pipe(_pipefd)) {
        return -1;
    }

    HR_LOGD("kunren :%ld, timeout:%ld\n",
            _eguard_options[OPTION_EGUARD_KUNREN_DETECT_ENABLED].value.number,
            _eguard_options[OPTION_EGUARD_KUNREN_DETECT_TIMEOUT].value.number);

    uv_poll_init(loop, &_state_machine_poll, _pipefd[0]);
    uv_poll_start(&_state_machine_poll, UV_READABLE, _statemachine_message_handle);

    uv_timer_init(loop, &_timer);

    return 0;
}

int statemachine_deinit() {
    uv_timer_stop(&_timer);
    uv_poll_stop(&_state_machine_poll);
    uv_close((uv_handle_t*)&_state_machine_poll, NULL);

    close(_pipefd[0]);
    close(_pipefd[1]);
    _pipefd[0] = -1;
    _pipefd[1] = -1;
    return 0;
}

int statemachine_post(enum state_machine_event message) {
    if (_pipefd[1] == -1) {
        return -1;
    }

    write(_pipefd[1], &message, sizeof(message));
    return 0;
}