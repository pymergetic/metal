# NuttX target — stub. See docs/SOURCETREE.md, docs/LAYERS.md.

Not yet brought up, but expected to be cheaper than `src/zephyr/` when it is: WAMR already
ships a maintained `external/wamr/core/shared/platform/nuttx/` backend that reuses
`common/posix/posix_file.c` (WASI file I/O — real `openat()`/`fstatat()`/... via NuttX's own
libc) and real `pthread_t`/`sem_t` (threading) unchanged. No custom `os_*` file shim like
`src/zephyr/pymergetic/metal/wasi/file.c` should be needed here — see
`docs/MOUNT.md` § "Zephyr prerequisite" for the contrast.

NuttX's own build model is Kconfig + `make`-driven (an out-of-tree/in-tree "app", not a bare
CMake target) — this target's own integration shape still needs designing when bring-up
starts; likely closer to `src/zephyr/`'s Kconfig/board pattern than to `src/linux/`'s plain
CMake, but with much thinner `port/*.c` bindings underneath.
