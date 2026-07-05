# third_party

Pinned upstream sources (git submodules).

| Path | Remote | Tag |
|------|--------|-----|
| `cpython/` | https://github.com/python/cpython | `v3.14.6` |
| `zephyr/` | https://github.com/zephyrproject-rtos/zephyr | `v4.4.0` |

```bash
git submodule update --init --recursive
```

Zephyr still needs a west workspace for HAL/modules — `third_party/zephyr` is the manifest root; run `west update` from your west topdir with this tree as `ZEPHYR_BASE` or init west against it.
