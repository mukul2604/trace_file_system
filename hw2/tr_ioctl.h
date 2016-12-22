#ifndef _TR_IOCTL_H_
#define _TR_IOCTL_H_
#include <linux/ioctl.h>

typedef struct
{
	unsigned long bitmap;
  const char *mntpath;
} tr_arg_t;

extern tr_arg_t trctl;

#define TRCTL_FILE "trace"
#define TRCTL_FILEPATH "/dev/trace"
#define TRCTL_MAGIC 0x5443544c

#define TR_GET_BMAP _IOR(TRCTL_MAGIC, 1, tr_arg_t *)
#define TR_SET_BMAP _IOW(TRCTL_MAGIC, 2, tr_arg_t *)
#endif
