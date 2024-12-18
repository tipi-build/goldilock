Goldilock - parent process id based concurrency barrier
=======================================================

`goldilock` is a lock-file based mutex that can be used to manage access to shared ressources by blocking access until an exclusive lock can be aquired.

- `goldilock` can acquire multiple locks in the same command
- `goldilock` avoids deadlocking by:
    - reshuffling the position in queue when wait times exceed some threshold
    - automatically expires wait queue positions when the owner process (read "another `goldilock`) doesn't refresh it regularly
    - optionally linking lock holding to the lifetime of a parent process
- `goldilock` can launch a process once it aquired (all) lock(s)
- `--watch-parent-process` watch parent process with the given name(s) (furthest matching parent will count unless `--search-nearest-parent-process` is added)
- `--detach` to handle the locking in a background process
- `--unlockfile <path>` as an alternative to launching a process to support file based IPC in the case of `--detach` -ed workflows
- `--timeout` possible when using `--unlockfile` (defaults to 60s)


```help
> goldilock --help                 
You must specify the [lockfile] positional argument
goldilock - flexible file based locking and process barrier for the win
Usage:
  goldilock [OPTIONS] -- <command(s)...>    any command line command that
                                            goldilock should run once the 
                                            locks are acquired. After 
                                            command returns, the locks are
                                            released and the return code
                                            forwarded. Standard I/O is 
                                            forwarded unchanged

  -v, --verbose                 Verbose output
  -h, --help                    Print usage
  -l, --lockfile arg            Lockfile(s) to acquire / release, specify 
                                as many as you want
      --unlockfile arg          Instead of running a command, have 
                                goldilock wait for all the specified unlock 
                                files to exist (those files will be deleted 
                                on exit)
      --timeout arg             In the case of --unlockfile, specify a 
                                timeout that should not be exceeded (in 
                                seconds, default to 60) (default: 60)
      --no-timeout              Do not timeout when using --unlockfile
      --detach                  Launch a detached copy with the same 
                                parameters otherwise
      --lock-success-marker arg
                                A marker file to write when all logs got 
                                acquired
      --watch-parent-process arg
                                Unlock if the selected parent process exits
      --search-nearest-parent-process
                                By default --watch-parent-process looks up 
                                for the furthest removed parent process, 
                                set this flag to search for the nearest 
                                parent instead
      --version                 Print the version of goldilock
```

Building
--------

Build using `tipi` (substitute the toolchain your platform of choice e.g. `windows-cxx17` or `vs-16-2019-win64-cxx17` etc...)

```shell
tipi . -t linux-cxx17
```

... or by using the provided `cmake` build system:

```shell
mkdir -p build/linux
cmake -S . -B build/linux -GNinja -DCMAKE_TOOLCHAIN_FILE=environments/linux-clang-cxx17.cmake
cmake --build build/linux
```


License
-------

`goldilock` is available under a proprietary license or subject to GPLv2 licence as per your choice. Please contact [tipi technologies Ltd.](https://tipi.build/) for commercial options.
