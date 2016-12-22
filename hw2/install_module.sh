!/bin/sh
set -x
# WARNING: this script doesn't check for errors, so you have to enhance it in case any of the commands
# below fail.
lsmod
rmmod trfs 
insmod ../fs/trfs/trfs.ko
rmmod trace_ctl
insmod trace_ctl.ko 
lsmod
