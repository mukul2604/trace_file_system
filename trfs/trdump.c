#include "trfs.h"
atomic_t trecord_id;

#define SAFE_FREE_PAGE(_p_)               \
do {                                      \
    if((_p_) != NULL) {                   \
        free_page((unsigned long)(_p_));  \
        (_p_) = NULL;                     \
    }                                     \
} while(0)

int fill_record(rec_type_t type, struct dentry *dentry, int flags, int perm_flags, int err,
                void *aux, int aux_len, struct tr_record *record)
{
	char *pathname = NULL, *aux_pathname = NULL;
  uint32_t rec_ID;
  int ret = 0;
  
  if (type == 0) {
      printk("rec_type_t: %d\n",type);
      return -1;
  }

  /**
   * primary pathname placeholder relative to
   * trfs mount point
   */ 
  pathname = (char *)__get_free_page(GFP_ATOMIC);
  if (pathname == NULL) {
    printk("Couldn't get the free page\n");
    ret= -ENOMEM;
    goto cleanup;
  }

  /**
   * auxiliary pathname placeholder relative to trfs
   * mount point
   */ 
  aux_pathname = (char *)__get_free_page(GFP_ATOMIC);
  if (aux_pathname == NULL) {
    printk("Couldn't get the free page\n");
    ret= -ENOMEM;
    goto cleanup;
  }

 
  rec_ID = (uint32_t)atomic_inc_return(&trecord_id);
  memset((void*)record, 0, sizeof(*record));
  record->rec_id = rec_ID;
  record->type = type;
  record->perm_flags = perm_flags;
  record->rec_flags = flags;
  record->pathname  = dentry_path_raw(dentry, pathname, PAGE_SIZE); 
  record->path_len = strlen(record->pathname);
  record->result = err;

#define RECORD_SIZE(record)                               \
    sizeof(struct tr_record) + (record)->path_len         \
    - sizeof((record)->pathname) - sizeof((record)->aux)

#define RECORD_SIZE_AUX(record)                                       \
    sizeof(struct tr_record) + (record)->path_len + (record)->aux_len \
    - sizeof((record)->pathname) - sizeof((record)->aux)
  
  switch (type) {
    case TR_FCREAT:
    /* Dir op types*/
    case TR_DCREAT:
    /* commom ops */
    case TR_UNLINK:
        record->rec_size = RECORD_SIZE(record);
        break;
    case TR_FWRITE:
        record->aux = (char*)aux;
        record->aux_len = aux_len;
        record->rec_size = RECORD_SIZE_AUX(record);
        break;
    case TR_RENAME:
    case TR_LINK:
        record->aux = dentry_path_raw((struct dentry*)aux, aux_pathname, PAGE_SIZE);
        record->aux_len = strlen(record->aux);
        record->rec_size = RECORD_SIZE_AUX(record);
        break;
    case TR_SYMLINK:
        record->aux = (char*)aux;
        record->aux_len = aux_len;
        record->rec_size = RECORD_SIZE_AUX(record);
        break;
    default:
        printk("BUG!!!!!!!!!!!!!!!!!!!!!!!!!! in: %s\n",__func__);
        break;
  } 
  printk("%s:%d rec_id=%d, rec_flags=%d, perm_flags=%d, result=%d, rec_size=%d, path_len=%d,"
           " rec_type=%d, pathname=%s, aux_len=%d\n",
           __func__, __LINE__,
           record->rec_id, record->rec_flags, record->perm_flags, record->result,
           record->rec_size, record->path_len, record->type, record->pathname, record->aux_len);
cleanup:
  if (ret != 0) {
    SAFE_FREE_PAGE(pathname);
    SAFE_FREE_PAGE(aux_pathname);
  }
  return ret;
}

int append_record(void *upper_sb, struct tr_record *record) {
    struct super_block *sb = (struct super_block*)upper_sb;
    int ret;
    short rec_size_common;
    struct file *logfilp;
    
    BUG_ON(strcmp(sb->s_type->name, TRFS_NAME) != 0);

    logfilp = TRFS_SB(sb)->tr_lower_filep;
    rec_size_common = record->rec_size - record->path_len - record->aux_len;
    BUG_ON(rec_size_common <= 0);
 
    memcpy(TRFS_PAGE(sb), record , rec_size_common);
    strcpy(TRFS_PAGE(sb)+rec_size_common, record->pathname);
    
  if (record->pathname == NULL) {
      BUG_ON("MEMORY LEAK\n");
    } else {
      free_page((unsigned long)record->pathname);
    }

    ret = kernel_write(logfilp, TRFS_PAGE(sb), record->rec_size - record->aux_len,
                       logfilp->f_pos);
    if (ret !=  record->rec_size - record->aux_len) {
      ret = -1; 
    } else {
      logfilp->f_pos += ret;
    }
    
    if(record->aux_len == 0) {
      BUG_ON(record->aux != NULL);
      return ret;
    }

    ret = kernel_write(logfilp, record->aux, record->aux_len, logfilp->f_pos);
    if (ret != record->aux_len) {
      ret = -1;
    } else {
      logfilp->f_pos += ret;
    }

    switch (record->type) {
      case TR_FWRITE:
        break;
      default:
        free_page((unsigned long)record->aux);
        break;
    }        
    return ret;
}
