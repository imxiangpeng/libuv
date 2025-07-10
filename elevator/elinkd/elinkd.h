#ifndef ELINKD_H
#define ELINKD_H

typedef int (*task_handler)(void* args);

int post_timer_task(int, task_handler, void*);
int post_async_task(task_handler, void*);
int elinkd_main(int argc, char** argv);
#endif