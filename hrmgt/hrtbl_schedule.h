#ifndef _HRTBL_SCHEDULE_H_
#define _HRTBL_SCHEDULE_H_

struct hrtbl_schedule_module {
    const char *table;
    void (*reload)(const char *table);
};
#endif

