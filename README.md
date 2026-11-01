# LKbench

LKbench is a micro-benchmark tool for measuring Linux kernel metrics.

## Usage

```bash
./build/lkbench                        # run all benchmarks
./build/lkbench --list                 # list all test IDs
./build/lkbench -b syscall             # run one test group
./build/lkbench -b fs.read             # run a single test
./build/lkbench -b syscall -b mem      # union of selectors
./build/lkbench -b net -f json         # JSON output
./build/lkbench -b fs -s min,median    # select metrics
```

## Build

Requires CMake >= 3.16, and GCC or Clang.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Test Groups

| Group | Description | Tests |
| --- | --- | --- |
| `syscall` | Plain syscall latency | `getcwd`, `getppid`, `uname` |
| `fs` | Filesystem operations on temp files under `/tmp` (page-cache hits) | `open-close`, `read`, `write`, `stat`, `fstat`, `create-unlink`, `mkdir-rmdir`, `rename`, `chmod`, `dup`, `ftruncate`, `readdir`, `readlink` |
| `mem` | Memory management | `mmap-munmap`, `mmap-touch`, `mprotect` |
| `net` | Loopback networking (127.0.0.1, forked echo server) | `tcp-rr-small`, `udp-rr-small` (64 B round trips), `tcp-crr` (connect / 1-byte exchange / close), `tcp-stream`, `tcp-stream-reverse` (64 KiB transfers), `udp-stream` (1400 B datagram), `udp-loss` (burst loss rate) |

Run `./build/lkbench --list` to see all 26 test IDs.

## License

Mozilla Public License 2.0. See [LICENSE](LICENSE).
