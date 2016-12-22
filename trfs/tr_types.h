#ifndef _TRTYPE_H_
#define _TRTYPE_H_
#include <linux/types.h>

typedef enum rec_type {
  TR_FCREAT   = 1 << 0,
  TR_FREAD    = 1 << 1,
  TR_FWRITE   = 1 << 2,
  /* DIR operations */
  TR_DCREAT   = 1 << 10,
  TR_DREAD    = 1 << 11,
  /* common ops*/
  TR_RENAME  = 1 << 20,
  TR_LINK    = 1 << 21,
  TR_SYMLINK = 1 << 22,
  TR_UNLINK  = 1 << 23,
} rec_type_t;

struct tr_record {
  uint32_t      rec_id;
  uint32_t      rec_flags;
  uint32_t      perm_flags;
  int32_t       result;
  short         rec_size;
  short         path_len;
  rec_type_t    type;
  short         aux_len;
  const char*   pathname;
  const char*   aux;
}; 
#endif
