Goldilock - parent process id based concurrency barrier
=======================================================

`goldilock` is a PID lock-file based mutex that can be used to manage access to shared ressources by blocking access until an exclusive lock can be aquired.
The PID written to the lock file is the PID of the parent process of `goldilock`.

- a lock is acquired by `goldilock <path to lockfile> acquire`
    - `goldilock` returns as soon as the lock could be acquired and spin waits otherwise.
- a lock can be released manually by `goldilock <path to lockfile> release` (but only by the parent process that aquired the lock initially)
- a lock is considered expired when the PID that acquired the lock is no longer a running process
- `goldilock` is reentrant by parent process id (e.g. one same parent process can pass the barrier many times as long as it holds the lock)
- the order of locking depends on the OS scheduler

Building
--------

Build using `tipi` (substitute the toolchain your platform of choice e.g. `windows-cxx17` or `vs-16-2019-win64-cxx17` etc...)

```shell
tipi . -t linux-cxx17
```

... or by using the provided `cmake` build system:

```shell
mkdir cmake_build
cd cmake_build
cmake ..
cmake --build .
```


License
-------

`goldilock` is available under a proprietary license or subject to GPLv2 licence as per your choice. Please contact [tipi technologies Ltd.](https://tipi.build/) for commercial options.
