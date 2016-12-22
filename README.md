                              "Tracing File System"

WHAT IS TRFS?
- TRFS is a stackable filesystem on VFS layer. This filesystem logs filesystem related
  operations  e.g. read, write, link, unlink etc. into a logfile. The logfile resides
  in the lower file system on which TRFS is mounted.
- The purpose of logging the operation is to regenerate the same state filesystem by
  replaying the log.
- This can be useful in recovery kind of operations, where we can recreate the the same
  files, dirs, links, symlinks etc. in a new directory.

DOCUMENTATION:
- There are following components for this filesytem:
  1] trfs module: This module include all the TRFS related file/dir operations and it
                  also includes logging infrastructure for different file/dir ops.

  2] trace_ctl module: This module contains the ioctl implementation to control the
                       logging behavior from user side. Using this module, user can 
                       specify  which ops need to be logged and which are not needed.
                       By default, trfs_module captures all the ops into the logfile.
  3] treplay: This utility is used to replay the TRFS.
  4] trctl: This utility is used to control the behavior of logging in trfs module.
  
DESIGN DETAILS:
- TRFS_MODULE:
    - MOUNT: mount assumes that user gives the path of logfile using option 
      -o tfile="file", mount code parse this option and capture the path from user land
      and check the sanity of pathname. Then it tries to create the log file in lower
      file system.
      Besides this, mount path initializes some fields of trfs' super block which is   
      similar to wrapfs, however for efficient logging, it keeps one buffer of size 4KB
      + sizeof in memory record format. This buffer is owned by the superblock of the
      point. mount path also initializes the trfs' private filesytem information.
    
    - LOGGING: trfs logs the ops into logfile whenever a particular filesytem op is
      invoked. since trfs is written based on the wrapfs template. logging happens in
      the corresponding trfs_* (read/write/open/link/unlink) functions. at this level,
      all the relevant information can be captured which is need for replaying the same
      op in the userland.

      Logging of operation type, flags, perm_flags, size etc. is straight forward for
      a particular operation, however, logging of file identity is complex here.
      Here, trfs logs the path of file/dir relative to the mount point whose size can't
      go beyond 4KB.

      The reason of keeping record in this way makes the "treplay" utility stronger and
      efficient. If we keep inode only, there can be multiple files pointing to the 
      same inode, writing and reading the fille will be easy however special ops like
      hardlink won't work since there is no way of distinguishment at inode level for
      hardlinks. Similar situation can happen for symbolic links.

      Keeping file pointer in the logfile seems more erratic since it is a in-memory
      information only. "treplay" will not work properly for this approach.

      This module currently logs basic filesystem ops like create/write/link/symlink/
      rename/delete for files as well as directories.
    
      Record also keeps auxiliary information for special operation like write/rename/
      link/symlink where data/second pathname needs to be captured.  

    - REPLAY: replay is done by using "treplay" utility. this utility read the record
      from the logfile and issues the relevant filesystem op from user land. the op 
      type is already stored in the record when the op is being logged. record has
      all information plus relative path information.
      Replay assumes that it is being run from the target directory, where the f/s
      needs to be recreated. This create the absolute path from root by prepending
      the current working direcory path to the relative path which is stored in the 
      logfile.
      Once required absolute path and required info is parsed from the logfile, treplay
      replays the operation from user land.

      For resolving the path, it keeps one buffer of 4K size. and for special ops like
      rename/link etc, it allocates one extra buf for resolving the second path.
      
-TRACE_CTL MODULE:
      This module is basically an ioctl module. User can control trfs logging using
      ioctl methods. User send the command to ioctl module. Ioct, service the command
      issued from userland.
      
      -SET_BMAP:  this command set the bitmap flags. trfs keeps the bitmap in super
       block of each mount point. ioctl parse the mount path given from user land and
       set the value to related super block's bitmap of mount point.
      -GET_BMAP: for particular mount point it returns the bitmap value.   


INSTALLATION:
    There are two directories where code resides:
    1] /usr/src/hw2-muksharma/fs/trfs/
        -issue "make" in this dir. It will create trfs module.
    2] /usr/src/hw2-muksharma/hw2/
        -issue "make" in this dir. It will create trace_ctl module and create "treplay"
         and "trctl" binaries.


USAGE:
    After compiling the code, install the modules using the below script. 
    -  /usr/src/hw2-muksharma/hw2/install_module.sh
    Mount the "trfs" on top of some "ext4" f/s
    -  mount -t trfs -o tfile=/tmp/tfile1.txt /some/lower/path /mnt/trfs
    Do f/s related operations in /mnt/trfs/  

    For controlling the logging:
    - goto hw2 dir.
    - ./trctl <mount path>  :  this will get the bitmap values and print
    - ./trctl <cmd> <mount path> :  this will set the bitmap for mount path

    For REPLAY:
    -  Goto the dir where you want to replay
    -  use "treplay" utility in following way:
        <path of replay binary>  <path of logfile of mount point>  


FILES:
  - all changes are present in /usr/src/hw2-muksharma/fs/trfs/ and
    /usr/src/hw2-muksharma/hw2/ directories.
