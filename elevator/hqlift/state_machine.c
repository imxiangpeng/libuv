#include "state_machine.h"
#include <stdlib.h>
#include <unistd.h>
#include <uv.h>
#include "state_machine.h"

#define DOOR_OPEN_TIMEOUT_AFTER_STOPPED 3000  // 3s

static int _pipefd[2] = {-1};

static enum state_machine_state _state;
static uv_poll_t _state_machine_poll;
static uv_work_t _background_detector_work;

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
    if (!handle)
        return;
    printf("%s(%d): come in door not opened after stopped...\n", __FUNCTION__, __LINE__);
}
static void _statemachine_message_handle(uv_poll_t* handle, int status, int events) {
    (void)handle;
    (void)status;
    (void)events;
    printf("%s(%d): come in ...\n", __FUNCTION__, __LINE__);
    if (!(events & UV_READABLE)) {
        return;
    }

    int message = -1;
    size_t rc = read(handle->io_watcher.fd, &message, sizeof(message));
    if (rc != sizeof(message)) {
        return;
    }

    printf("state machine message: %d(%s) -> %d(%s)\n", _state, state_str(_state), message, state_str(message));

    switch (_state) {
        case SM_ELEVATOR_UNINIT:
            _state = message;
            break;
        case SM_ELEVATOR_STOPPED:
            switch (message) {
                case SM_ELEVATOR_STOPPED_DOOR_OPENED:
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
                case SM_ELEVATOR_STOPPED_DOOR_CLOSED:
                    break;
                case SM_ELEVATOR_RUNNING:
                    printf("%s(%d): fault! running but door opened! current state:%d, not support directly to state %d\n", __FUNCTION__, __LINE__, _state, message);
                
                    break;
                default:
                    printf("%s(%d): current state:%d, not support directly to state %d\n", __FUNCTION__, __LINE__, _state, message);
                    break;
            }
            break;
        case SM_ELEVATOR_STOPPED_DOOR_CLOSED:
            switch (message) {
                case SM_ELEVATOR_RUNNING:
                    _state = SM_ELEVATOR_RUNNING;
                    break;
                default:
                    printf("%s(%d): current state:%d, not support directly to state %d\n", __FUNCTION__, __LINE__, _state, message);
                    break;
            }
            break;
        case SM_ELEVATOR_RUNNING:
            switch (message) {
                case SM_ELEVATOR_STOPPED:
                    _state = SM_ELEVATOR_STOPPED;
                    // start timer to detect door open
                    uv_timer_start(&_timer, _wait_door_opened_after_stopped_cb, DOOR_OPEN_TIMEOUT_AFTER_STOPPED, 0);  // 每1000ms触发一次
                    break;
                default:
                    printf("%s(%d): current state:%d, not support directly to state %d\n", __FUNCTION__, __LINE__, _state, message);
                    break;
            }
            break;
        default:
            break;
    }
    
    // _state = message;
}
static void detector_work(uv_work_t* req) {
    (void)req;
}
static void detector_work_finished(uv_work_t* req, int status) {
    (void)req;
    (void)status;
}

static void detector_timer_handler(uv_timer_t* handler) {
    if (!handler) {
        return;
    }

    uv_queue_work(handler->loop, &_background_detector_work, detector_work, detector_work_finished);
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

int statemachine_post(int message) {
    if (_pipefd[1] == -1) {
        return -1;
    }

    write(_pipefd[1], &message, sizeof(message));
    return 0;
}