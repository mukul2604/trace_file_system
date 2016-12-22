#include<stdint.h>
#include  "../fs/trfs/tr_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <ctype.h>
#include <unistd.h>
#include <err.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>

#define MIN(a,b) (a > b ? b : a)
#define BUFFER_SIZE 4096
#define help_str                                                                    \
  "Possible invalid use. Help:\n"                                                   \
  " -h: help\n"

void usage(void) {
	printf(help_str);
	return;
}

typedef enum troption {
    NOREPLAY = 1 << 0,
    STRICT   = 1 << 1,
} option_t;

#define SAFE_FREE(_buf_)    \
do {                        \
  if ((_buf_) != NULL) {    \
      free((_buf_));        \
      (_buf_) = NULL;       \
  }                         \
} while(0)

void print_record(struct tr_record *record, char *syscall, int err, bool noreplay) {
  printf("rec_id=%d, rec_flags=%d, perm_flags=%d, result=%d, "
         "rec_size=%d, path_len=%d, "
         "rec_type=%d, aux_len=%d, syscall=%s, record_err=%d, ",
         record->rec_id, record->rec_flags,
         record->perm_flags, record->result,
         record->rec_size, record->path_len,
         record->type, record->aux_len, syscall,
         record->result);
  if (noreplay == false) {
    printf("current_err=%d",err);
  }
  printf("\n");
}

int
replayed_status(struct tr_record *record, char *syscall, int err, bool strict) {
  if (strict) {
    if (err >= 0 && record->result >= 0) {
      printf("SUCESS::ID=%d\n",record->rec_id);
      print_record(record, syscall, err, false);
    } else {
      printf("FAILED::ID:%d\n",record->rec_id);
      print_record(record, syscall, err, false);
      perror("replay failed");
      return -1;
    }
    return 0;
  }

  if (err >= 0 && record->result >= 0) {
    printf("SUCESS::ID=%d\n",record->rec_id);
    print_record(record, syscall, err, false);
  } else {
    printf("FAILED::ID:%d\n",record->rec_id);
    print_record(record, syscall, err, false);
  }
  return 0;
}
 
/*
 * Total Memory allocation:
 *  4k - pathname buffer
 *  4k - auxpathname buffer
 *  4k - rel_pathname buffer
 *  16k - read buffer
 */
int main(int argc, char *argv[])
{
	int               opt, fd, rfd, bytes_read, fsize, size=0, err=0;
  void              *buf=NULL, *pathbuf=NULL, *savedbuf=NULL,
                    *limit=NULL, *auxbuf=NULL, *resolvebuf = NULL;
  char              *tfile, *pathname, *rel_path, *aux, *auxpath;
  struct tr_record  *record;
  option_t          option = 0;
  struct stat       lstat;
  bool              noreplay, strict;
  printf("sizeof(enum):%lu\n",sizeof(rec_type_t));
  /* Option parsing */
	while ((opt = getopt(argc, argv, "ns")) != -1) {
		switch (opt) {
		case 'n':
			option |= NOREPLAY;
      noreplay = true;
			break;
		case 's':
			option |= STRICT;
      strict = true;
			break;
		default:
			usage();
			return 0;
			break;
		}
	}
  /* Both are exclusive */
  if ((option  &  NOREPLAY) && (option & STRICT)) {
      usage();
      return 0;
  }
 
  tfile = argv[optind];
  printf("trace_file=%s\n",tfile);
  
  if (stat(tfile, &lstat) == 0 ) {
      fsize = lstat.st_size;
      /* buffer used for reading the tracefile */
      buf = malloc(MIN(fsize, 4*BUFFER_SIZE));
      savedbuf = buf;
      /* buffer used for holding the pathname*/
      pathbuf = malloc(BUFFER_SIZE);
      /* buffer used for holding path relative to trfs mount*/
      resolvebuf = malloc(BUFFER_SIZE);
  } else {
      perror("stat");
      err = -1;
      goto cleanup;
  }

  fd = open(tfile, O_RDONLY);
  if (fd < 0) {
      err = fd;
      goto out;
  } 

  /**
   * Read the whole file in chunk of max CHUNK_SIZE,
   * parse the record and do appropriate action.
   */ 
  while (size < fsize) {
    bytes_read = read(fd, buf, 4 * BUFFER_SIZE);
    
    if(bytes_read < 0) {
        err = bytes_read;
        goto out;
    }

    limit = buf + bytes_read;

    while (buf  < limit) { 
      record = (struct tr_record *)buf;

      rel_path = (char*)buf + sizeof(struct tr_record) 
                 - sizeof(record->pathname)
                 - sizeof(record->aux);
      /**
       * aux points to either data or auxiliary
       * pathname.
       */  
      aux = rel_path + record->path_len;
    
      /**
       * this will resolve primary path
       */ 
      memcpy(resolvebuf, rel_path, record->path_len);
      rel_path = (char*)resolvebuf;
      rel_path[record->path_len] ='\0';
      pathname = getcwd(pathbuf, BUFFER_SIZE);
      strcat(pathname,rel_path); 
      /**
       * Only TR_FWRITE uses aux as actual data to be written,
       * this can be used by read also.
       * else aux will be used as auxiliary pathname.
       */   
      if(record->aux_len && record->type != TR_FWRITE) {
        /*
         * buffer used for holding second relative path
         * for operations like rename, link, symlink, move
         * etc.
         */
        memcpy(resolvebuf, aux, record->aux_len);
        aux = (char*)resolvebuf;
        aux[record->aux_len] = '\0';

        if (record->type != TR_SYMLINK) {
          auxbuf = malloc(BUFFER_SIZE);
          auxpath = getcwd(auxbuf, BUFFER_SIZE);
          strcat(auxpath, aux);
        }
      }

      switch(record->type) {
        /**
         * File related operations.
         */ 
        case TR_FCREAT:
            if (noreplay) {  
                print_record(record, "open", record->result, noreplay);
                break;
            }
            /* TODO: is O_CREAT is sufficient for the creation of file*/
            rfd = open(pathname, O_CREAT, record->perm_flags);
            if (rfd >=0) close(rfd);
            err = rfd;
            if (replayed_status(record, "open", err, strict) !=0) {
              goto out;
            }
            break;
        case TR_FWRITE:
            if (noreplay) {  
                print_record(record, "open", record->result, noreplay);
                print_record(record, "write", record->result, noreplay);
                break;
            }

            rfd = open(pathname, record->rec_flags);
            err = rfd;
            if (replayed_status(record, "open", err, strict) !=0) {
              if (rfd >=0) close(rfd);
              goto out;
            }
            
            err = write(rfd, aux, record->aux_len);
            if (replayed_status(record, "write", err, strict) !=0) {
              if (rfd >=0) close(rfd);
              goto out;
            }

            if (rfd >=0) close(rfd);
            break;
        /**
         * Dir related operations.
         */ 
        case TR_DCREAT:
            if (noreplay) {  
                print_record(record, "mkdir", record->result, noreplay);
                break;
            }

            err = mkdir(pathname, record->perm_flags);
            if (replayed_status(record, "mkdir", err, strict) !=0) {
              goto out;
            }
            break;
        /**
         * common operations.
         */ 
        case TR_RENAME:
            if (noreplay) {  
                print_record(record, "rename", record->result, noreplay);
                break;
            }
            err = rename(pathname, auxpath);
            if (replayed_status(record, "rename", err, strict) !=0) {
              goto out;
            }
            break;
        case TR_LINK:
            if (noreplay) {  
                print_record(record, "link", record->result, noreplay);
                break;
            }
            err = link(pathname, auxpath);
            if (replayed_status(record, "link", err, strict) !=0) {
              goto out;
            }
            break;
        case TR_SYMLINK:
            if (noreplay) {  
                print_record(record, "symlink", record->result, noreplay);
                break;
            }
            err = symlink(aux, pathname);
            if (replayed_status(record, "symlink", err, strict) !=0) {
              goto out;
            }
            break;
        case TR_UNLINK:
            if (noreplay) {  
                print_record(record, "remove", record->result, noreplay);
                break;
            }
            /* can be used remove as well*/
            err = remove(pathname);
            if (replayed_status(record, "remove", err, strict) !=0) {
              goto out;
            }
            break;
        default:
            printf("Invalid Operation\n");
            goto out;
            break;
      }
      buf += record->rec_size;
    } /* end of while(buf < limit) */
    size += bytes_read;
    buf = savedbuf;
  }  /* end of while(size < fsize)*/

out:
  close(fd);
  SAFE_FREE(savedbuf);
  SAFE_FREE(pathbuf);
  SAFE_FREE(auxbuf);
  SAFE_FREE(resolvebuf);
cleanup:
  exit(err); 
}
