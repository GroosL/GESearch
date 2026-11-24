# search

A fast, multi-threaded CLI file and directory name search utility written in C using POSIX threads and the Boyer–Moore–Horspool string-matching algorithm.

## Features

- **Multi-threaded traversal**: Automatically spawns one worker thread per online CPU core (`sysconf(_SC_NPROCESSORS_ONLN)`) backed by a dynamically growing circular work queue.
- **Boyer–Moore–Horspool (BMH) matching**: Precomputes shift and lowercase lookup tables once at startup for fast case-sensitive and case-insensitive substring matching.
- **Zero `stat()` directory walk**: Uses `dirent->d_type` (`DT_DIR`) to skip symlinks and regular files during recursion without extra `stat` syscalls or symlink cycles.
- **Streaming output**: Writes matches directly to `STDOUT_FILENO` as they are discovered, making it responsive in pipelines.
- **Ignore path filtering**: Supports multiple `-i <dir>` exclusions matched on path segment boundaries.
- **Minimal dependencies**: Pure C + POSIX (`pthread`), compatible with both glibc and musl.

## Requirements

- A C compiler (`cc`, `gcc`, or `clang`) with `pthreads`
- GNU `make` (optional)

## Building

```sh
make
```

Or compile directly:

```sh
cc -Wall -O3 -o search main.c queue.c -lpthread
```

Other `Makefile` targets:

- `make debug` — build with debug symbols (`-g -O0`)
- `make clean` — remove build artifacts

## Usage

```sh
search <path> <pattern> [-s] [-r] [-e] [-i <dir>]
```

### Flags

| Flag | Description |
| :--- | :--- |
| `-s` | Case-insensitive search |
| `-r` | Recursive search (only traverses real directories, does not follow symlinks) |
| `-e` | Exact filename match |
| `-i <dir>` | Ignore paths containing `<dir>` as a path component (repeatable, up to 32 rules) |

### Examples

Search the current directory (non-recursive):

```sh
./search . main
```

Search recursively with case-insensitivity:

```sh
./search /nix/store FoOBAR -r -s
```

Exact filename match, ignoring `.git` and `node_modules`:

```sh
./search . package.json -r -e -i .git -i node_modules
```

## Benchmarks

Benchmarked on **Linux 7.3.4** (`x86_64`, **AMD Ryzen 7 5700** — 8 cores / 16 threads).

### Non-recursive (`/nix/store` — 32,946 top-level entries, warm cache, 10 runs after warmup)

| Command | Mean | Min | Max | Relative |
| :--- | ---: | ---: | ---: | ---: |
| `./search /nix/store proton` | **17.35 ms** | 16.49 ms | 21.85 ms | **1.00×** |
| `ls -U1 /nix/store \| grep -F proton` | 29.25 ms | 28.31 ms | 33.34 ms | 1.69× slower |
| `ls -1 /nix/store \| grep -F proton` | 51.51 ms | 50.08 ms | 59.76 ms | 2.97× slower |
| `find /nix/store -maxdepth 1 -name '*proton*'` | 55.19 ms | 54.49 ms | 56.42 ms | 3.18× slower |
| `eza -1 /nix/store \| rg -F proton` | 201.89 ms | 199.99 ms | 206.74 ms | 11.64× slower |

### Recursive (`/nix/store` — 2,719,496 total entries, warm cache)

| Command | Runs | Mean | Times (ms) | Relative |
| :--- | :---: | ---: | :--- | ---: |
| `./search /nix/store proton -r` | 5 | **1.50 s** | `1472, 1493, 1471, 1516, 1527` | **1.00×** |
| `./search /nix/store PROTON -r -s` | 5 | **1.55 s** | `1597, 1513, 1573, 1548, 1519` | 1.04× |
| `./search /nix/store proton -r -e` | 5 | **1.59 s** | `1555, 1631, 1589, 1623, 1548` | 1.06× |
| `rg --files --no-ignore --hidden /nix/store \| rg -F proton` | 3 | 1.85 s | `1863, 1872, 1812` | 1.24× slower |
| `find /nix/store -name '*proton*'` | 3 | 14.01 s | `13949, 13943, 14139` | 9.37× slower |

> **Note**: Both `search` and `find` match both file and directory names during traversal, whereas `rg --files` only emits regular file paths.

### Full Root Filesystem Recursive (`/` with ignored mounts)

Command:

```sh
./search / main2.c -r -i ~/remote -i ~/HDD -i ~/zdata
```

- **Cold/partial cache (unredirected output)**: `13.44 s` (`user 3.35s`, `sys 49.29s`)
- **Warm cache (`>/dev/null 2>&1`, 17 matches)**:

| Command | Runs | Mean | Individual Runs (`real`) | Relative |
| :--- | :---: | ---: | :--- | ---: |
| `./search / main2.c -r -i ~/remote -i ~/HDD -i ~/zdata` (batch 1) | 3 | **5.27 s** | `5.46s, 6.11s, 4.22s` | — |
| `./search / main2.c -r -i ~/remote -i ~/HDD -i ~/zdata` (batch 2, `time -p`) | 3 | **4.68 s** | `5.91s, 3.40s, 4.72s` | **1.00×** |
| `find / \( -path ~/remote -o -path ~/HDD -o -path ~/zdata \) -prune -o -name '*main2.c*' -print` | 2 | 36.65 s | `45.27s, 28.02s` | 7.84× slower |

## License

Licensed under the [BSD Zero Clause License](LICENSE) (`0BSD`).
