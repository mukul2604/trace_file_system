#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include "tr_ioctl.h"

typedef enum trctl_option {
    GET_BMAP = 1 << 0,
    SET_BMAP = 1 << 1,
} tr_option_t;
  
void get_bitmap(int fd, tr_arg_t *arg) {
	if (ioctl(fd, TR_GET_BMAP, arg) == -1) {
		perror("trace ioctl bitmap get");
	} else {
		printf("Bitmap : 0x%lx\n", arg->bitmap);
	}
}

void set_bitmap(int fd, tr_arg_t *arg) {
	if (ioctl(fd, TR_SET_BMAP, arg) == -1) {
		perror("trace ioctl bitmap set");
	}
}

bool isvalidhex(const char *hex) {
  bool valid = true;
  int i;
  int slen;
    
  slen = strlen(hex);
  if(slen > 18) {
    return false;
  }

  for(i = 2; i < slen; i++) {
    if (!isxdigit(hex[i])) {
      valid = false;
      break;
    }
  }
         
  return ((hex[0] == '0') && (tolower(hex[1])=='x') && valid);
}

int main(int argc, char *argv[]) {
	char *file_name = TRCTL_FILEPATH;
	int trfd;
  tr_option_t option;
  tr_arg_t args;

  if (argc == 2) {
    option = GET_BMAP;
    args.mntpath = argv[1]; 
  } else if(argc == 3) {   /* set cases */
    if (strcmp(argv[1], "all") == 0) {
      args.bitmap = -1; 
    } else if (strcmp(argv[1], "none") == 0) {
      args.bitmap = 0;
    } else {
      if (isvalidhex(argv[1])) {
        args.bitmap = strtoul(argv[1], NULL, 16);
      } else {
        fprintf(stderr, "Usage: %s [all | none | 0xNN]\n", argv[0]);
        return 1;
      }
    }
    option = SET_BMAP;
    args.mntpath = argv[2]; 
  } else {
    fprintf(stderr, "Usage: %s [all | none | 0xNN]\n", argv[0]);
    return 1;
  }
  
  printf("Mount path:%s\n", args.mntpath);
 
	trfd = open(file_name, O_RDWR);
  if (trfd < 0) {
		perror("trctl device file open");
		return trfd;
	}

	switch (option) {
		case GET_BMAP:
			get_bitmap(trfd, &args);
			break;
		case SET_BMAP:
			set_bitmap(trfd, &args);
			break;
		default:
			break;
	}

	close (trfd);

	return 0;
}
