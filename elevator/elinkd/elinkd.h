#ifndef ELINKD_H
#define ELINKD_H


struct uloop_timeout;

typedef int (*task_handler)(void* args);

void timer_post(struct uloop_timeout* t, int msec);
void timer_cancel(struct uloop_timeout* t);
int post_timer_task(int, task_handler, void*);
int post_async_task(task_handler, void*);
int elinkd_main(int argc, char** argv);
#endif