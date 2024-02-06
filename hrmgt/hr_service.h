#ifndef _HR_SERVER_
#define _HR_SERVER_

#include "hr_list.h"

#define HRSRV_NAME_LEN (32)
#define HRSRV_MAX_ARGS (12)

#define HSRV_DISABLED 0x01  // 1 << 1
#define HSRV_ONESHOT 0x02   // 1 << 2
#define HSRV_RUNNING 0x04   // 1 << 3
#define HSRV_SCHEDULED 0x08 // 1 << 4

typedef struct hr_service {
  const char *name;
  const char *classname;

  int argc;
  char *argv[HRSRV_MAX_ARGS];

  unsigned int flags;

  struct hr_list_head next;

} hr_service_t;

typedef struct {

  hr_service_t srv;

  struct {
    unsigned int week; // bitmap
    unsigned int ; // bitmap
  } schedule_bitmap;

} hrsrv_scheduled_t;
#endif // _HR_SERVER_
