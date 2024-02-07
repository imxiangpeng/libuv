// simple database

#ifndef _HR_TBL_
#define _HR_TBL_

#include "j2s/j2sobject.h"

#define J2STBL_DECLARE_OBJECT struct hrtbl __object__
#define J2STBL_OBJECT_SELF(self) ((struct hrtbl_object *)self)

#define J2STBL_OBJECT_PRIV_FIELDS \
{.name = "__id__", .type = J2S_INT, .offset = offsetof(struct hrtbl_object, __id__), .offset_len = 0}

enum {
  TBL_OPBIT_INSERT = 0x1,
  TBL_OPBIT_UPDATE = 0x2,
  TBL_OPBIT_DELETE = 0x4
};
struct hrtbl_object {
  J2SOBJECT_DECLARE_OBJECT;
  int __id__; // auto increase id
};
struct hrtbl {

  char *path;

  int state;
  // struct j2sobject object;
  struct hrtbl_object object;
};

struct hrtbl *hrtbl_init(const char *table, struct j2sobject_prototype *proto);
void hrtbl_deinit(struct hrtbl *tbl);

int hrtbl_empty(struct hrtbl *tbl);

// object.id will auto increase ++
int hrtbl_insert(struct hrtbl *tbl, struct hrtbl_object *self);
#endif // _HR_TBL_
