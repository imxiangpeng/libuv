#include <stdio.h>
#include <string.h>

#include <uv.h>

#define LOOP_DEFAULT 0
#define LOOP_IO 1
#define LOOP_MAX 2

static uv_loop_t _loop[LOOP_MAX] = {{0}};

static int id = 0;
static void _timer_cb(uv_timer_t *t) {

  id++;
  if (id > 100) {
    // uv_close((uv_handle_t *)t, NULL);
    printf("%s(%d): timer id: %d...stop!\n", __FUNCTION__, __LINE__, id);
    uv_timer_stop(t);
    return;
  }
  // uv_timer_again(t);
}
static void fs_event_cb_file_current_dir(uv_fs_event_t *handle,
                                         const char *filename, int events,
                                         int status) {
  // uv_close((uv_handle_t*)handle, NULL);
  printf("%s(%d): file:%s, events:0x%X, status:0x%X\n", __FUNCTION__, __LINE__,
         filename, events, status);
}

int main(int argc, char **argv) {
  int ret = 0;
  size_t i = 0;
  uv_timer_t timer_event;
  uv_fs_event_t fs_event;

  memset((void *)&timer_event, 0, sizeof(timer_event));

  memset((void *)&_loop, 0, sizeof(uv_loop_t) * LOOP_MAX);
  for (i = 0; i < sizeof(_loop) / sizeof(_loop[0]); i++) {
    uv_loop_init(&_loop[i]);
  }

  memset((void *)&fs_event, 0, sizeof(fs_event));
  uv_fs_event_init(&_loop[LOOP_DEFAULT], &fs_event);
  ret = uv_fs_event_start(&fs_event, fs_event_cb_file_current_dir, "./watcher",
                          0);
  if (ret != 0) {
    printf("%s(%d): fs event failed ...\n", __FUNCTION__, __LINE__);
  }

  uv_timer_init(&_loop[LOOP_DEFAULT], &timer_event);

  uv_timer_start(&timer_event, _timer_cb, 1000, 2000);

  uv_run(&_loop[LOOP_DEFAULT], UV_RUN_DEFAULT);

  uv_loop_close(&_loop[LOOP_DEFAULT]);
  uv_library_shutdown();
  
  return 0;
}
