# dlinject

`dlinject` loads a shared library into a running Linux process through `ptrace`.
The installed tool is one executable and does not invoke a shell, debugger, ELF
utility, or other helper program at runtime.

Supported same-ABI pairs:

- x86-64
- i386
- AArch64
- ARMv7 (ARM state)

The injector and target must use the same architecture, bitness, C library, and
mount namespace. The caller must also be permitted to trace the target. These
constraints are checked where possible and failures are reported without leaving
the process attached.

## Build

```sh
cmake -S . -B build
cmake --build build --parallel
```

## Test

```sh
ctest --test-dir build --output-on-failure
```

The test is an end-to-end injection into a five-thread target. It verifies that
the example library constructor ran, the target remained alive, and its thread
count was unchanged.

## Use

```sh
dlinject PID /absolute/path/to/library.so
```

The library path is canonicalized before injection. A successful load retains
the normal `dlopen` reference in the target; the small temporary executable
mapping used by the injector is removed.

## Demo

```sh
./scripts/demo.sh build
```

The demo target opts into tracing with `PR_SET_PTRACER_ANY` so it works on hosts
using Yama's common restricted-ptrace setting. This relaxation exists only in
the demo target.

## Docker

Build and run the complete containerized demo:

```sh
docker build -t dlinject .
docker run --rm --cap-add SYS_PTRACE --security-opt seccomp=unconfined dlinject
```

The container starts the threaded target, injects the example library, prints
its load marker, and shuts the target down.

Build every architecture with Buildx/QEMU configured on the host:

```sh
docker buildx build \
  --platform linux/amd64,linux/386,linux/arm64,linux/arm/v7 \
  --progress plain .
```

Native image builds run the end-to-end injection test. Emulated image builds
run unit tests and compile every architecture path, because user-mode QEMU
represents the emulator rather than the guest ELF through `/proc/PID/exe`.
End-to-end ARM validation therefore requires an ARM host or full-system VM.

`SYS_PTRACE` and the seccomp override are required only when running an injector
inside Docker; they are not baked into the image.

## Design

The injector stops the thread group, saves the selected thread's registers and
instruction bytes, and performs a remote `mmap` syscall. Other threads resume
while a tiny architecture-specific stub calls the target's `dlopen`, avoiding a
loader-lock deadlock. It then stops the group again, removes the temporary page,
restores the original state, and detaches all threads.

This project is intended for controlled debugging, observability, and testing
of processes you own. The example library writes only a load marker.
# elf-inject
