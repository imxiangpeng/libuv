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
#include "state_machine.h"

#include "hr_log.h"
#include "uelevator.h"

#define DOOR_OPEN_TIMEOUT_AFTER_STOPPED 3000           // 3s
#define SOMEONE_INSIDE_WHEN_DOOR_CLOSED_TIMEOUT 20000  // 3s

static int _pipefd[2] = {-1};

static enum state_machine_state _state;
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
const char* state_str(enum state_machine_state state) {
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
static void _wait_door_opened_after_stopped_cb(uv_timer_t* handle) {
    struct elevator_status st;
    if (!handle)
        return;

    uelevator_get_status(&st);
    printf("%s(%d): come in door not opened after stopped...\n", __FUNCTION__, __LINE__);
    HR_LOGD("%s(%d): come in door not opened after stopped..., door:%d, passenger:%d\n", __FUNCTION__, __LINE__, st.door_state, st.passenger_count);

    if (st.passenger_count > 0) {
        HR_LOGD("%s(%d): people is in elevator while door is not opened ...\n", __FUNCTION__, __LINE__);
    }
}

static void _detect_someone_inside_when_long_stopped(uv_timer_t* handle) {
    struct elevator_status st;
    if (!handle)
        return;
    uelevator_get_status(&st);
    printf("%s(%d): come in door closed and we are stopped but some one is still in elevator...:%d\n", __FUNCTION__, __LINE__, st.passenger_count);
}
static void _statemachine_message_handle(uv_poll_t* handle, int status, int events) {
    (void)handle;
    (void)status;
    (void)events;
    printf("%s(%d): come in ...\n", __FUNCTION__, __LINE__);
    if (!(events & UV_READABLE)) {
        return;
    }

    enum state_machine_event message = -1;
    size_t rc = read(handle->io_watcher.fd, &message, sizeof(message));
    if (rc != sizeof(message)) {
        return;
    }

    printf("state machine message: %d(%s) received %d\n", _state, state_str(_state), message);

    switch (_state) {
        case SM_ELEVATOR_UNINIT:
            // directly assign _state according message
            switch (message) {
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

            switch (message) {
                case SM_EVENT_DOOR_OPENED:
                    _state = SM_ELEVATOR_STOPPED_DOOR_OPENED;
                    uv_timer_stop(&_timer);  // stop the door open wait timer
                    break;
                default:
                    printf("%s(%d): current state:%d, not support directly to state %d\n", __FUNCTION__, __LINE__, _state, message);
                    break;
            }
            break;
        case SM_ELEVATOR_STOPPED_DOOR_OPENED:
            switch (message) {
                case SM_EVENT_DOOR_CLOSED:
                    _state = SM_ELEVATOR_STOPPED_DOOR_CLOSED;
                    // check anyone is still in elevator but elevator is not running
                    uv_timer_start(&_timer, _detect_someone_inside_when_long_stopped, SOMEONE_INSIDE_WHEN_DOOR_CLOSED_TIMEOUT, 0);  // 每1000ms触发一次
                    break;
                case SM_EVENT_RUNNING:
                    printf("%s(%d): Exception: door is opened when running !\n", __FUNCTION__, __LINE__);
                    HR_LOGE("%s(%d): Exception: door is opened when running !\n", __FUNCTION__, __LINE__);
                    break;
                default:
                    printf("%s(%d): current state:%d, not support directly to state %d\n", __FUNCTION__, __LINE__, _state, message);
                    break;
            }
            break;
        case SM_ELEVATOR_STOPPED_DOOR_CLOSED:
            switch (message) {
                case SM_EVENT_RUNNING:
                    _state = SM_ELEVATOR_RUNNING;
                    break;
                default:
                    printf("%s(%d): current state:%d, not support directly to state %d\n", __FUNCTION__, __LINE__, _state, message);
                    break;
            }
            break;
        case SM_ELEVATOR_RUNNING:
            switch (message) {
                case SM_EVENT_STOPPED:
                    _state = SM_ELEVATOR_STOPPED;
                    // start timer to detect door open
                    uv_timer_start(&_timer, _wait_door_opened_after_stopped_cb, DOOR_OPEN_TIMEOUT_AFTER_STOPPED, 0);  // 每1000ms触发一次
                    break;
                case SM_EVENT_DOOR_OPENED:
                    printf("%s(%d): Exception door is opened while running\n", __FUNCTION__, __LINE__);
                    break;
                default:
                    printf("%s(%d): current state:%d, not support directly to state %d\n", __FUNCTION__, __LINE__, _state, message);
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