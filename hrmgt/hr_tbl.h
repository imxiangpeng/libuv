// simple database

#ifndef _HR_TBL_
#define _HR_TBL_

#include "j2s/j2sobject.h"

struct hrtbl;

//struct hrtbl_object {
//  J2SOBJECT_DECLARE_OBJECT;
//};
struct hrtbl {

  char *path;

  struct j2sobject object;
};

struct hrtbl *hrtbl_init(const char *table, struct j2sobject_prototype *proto);
void hrtbl_deinit(struct hrtbl *tbl);

int hrtbl_empty(struct hrtbl *tbl);

int hrtbl_insert(struct hrtbl *tbl, struct j2sobject *self);
#endif // _HR_TBL_
