INC=/lib/modules/$(shell uname -r)/build/arch/x86/include

obj-m += trace_ctl.o
trace_ctl-y = tr_ioctl.o

all: trace_ctl_mod
	gcc -Wall -Werror -I$(INC)/generated/uapi -I$(INC)/uapi treplay.c -o treplay
	gcc -Wall -Werror -I$(INC)/generated/uapi -I$(INC)/uapi trctl.c -o trctl

trace_ctl_mod:
	make -Wall -Werror -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules


clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f treplay 
	rm -f trctl
