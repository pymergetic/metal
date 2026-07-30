#include <stddef.h>
#include <stdint.h>

#include <pymergetic/metal/boot/platform/private/dump_mem.h>
#include <pymergetic/metal/console/__init__.h>
#include <pymergetic/metal/dt/__init__.h>
#include <pymergetic/metal/log/__init__.h>

static size_t cstrlen(const char *s)
{
  size_t n = 0;

  if (s == NULL) {
    return 0;
  }
  while (s[n] != '\0') {
    n++;
  }
  return n;
}

static void emit(const char *s)
{
  size_t n;

  if (s == NULL) {
    return;
  }
  n = cstrlen(s);
  if (pm_metal_log_ready() != 0) {
    pm_metal_log((const uint8_t *)s);
    return;
  }
  if (pm_metal_console_ready() != 0) {
    pm_metal_console_write(0u, (const uint8_t *)s, n);
  }
}

static char hex_digit(uint32_t v)
{
  v &= 0xfu;
  return (char)(v < 10u ? ('0' + v) : ('a' + (v - 10u)));
}

static void append_hex_u64(char *dst, size_t cap, size_t *pos, uint64_t v)
{
  char tmp[16];
  int i;

  if (*pos + 2u >= cap) {
    return;
  }
  dst[(*pos)++] = '0';
  dst[(*pos)++] = 'x';
  for (i = 15; i >= 0; i--) {
    tmp[i] = hex_digit((uint32_t)(v & 0xfu));
    v >>= 4;
  }
  for (i = 0; i < 16; i++) {
    if (*pos + 1u >= cap) {
      break;
    }
    dst[(*pos)++] = tmp[i];
  }
}

static void append_cstr(char *dst, size_t cap, size_t *pos, const char *s)
{
  size_t i;

  if (s == NULL) {
    return;
  }
  for (i = 0; s[i] != '\0'; i++) {
    if (*pos + 1u >= cap) {
      break;
    }
    dst[(*pos)++] = s[i];
  }
}

static int compat_eq(const uint8_t *a, const char *b)
{
  size_t i;

  if (a == NULL || b == NULL) {
    return 0;
  }
  for (i = 0; b[i] != '\0'; i++) {
    if (a[i] != (uint8_t)b[i]) {
      return 0;
    }
  }
  return a[i] == 0;
}

void pm_metal_boot_dump_mem(void)
{
  uint32_t n;
  uint32_t i;

  n = pm_metal_dt_count_class(PM_METAL_DT_CLASS_MEM);
  for (i = 0; i < n; i++) {
    const DtNode *node;
    uint64_t base;
    uint64_t size;
    char line[96];
    size_t pos;
    const char *kind;

    node = pm_metal_dt_by_class(PM_METAL_DT_CLASS_MEM, i);
    if (node == NULL) {
      continue;
    }
    base = ((uint64_t)node->loc[1] << 32) | (uint64_t)node->loc[0];
    size = ((uint64_t)node->loc[3] << 32) | (uint64_t)node->loc[2];
    if (compat_eq(node->compat, "heap")) {
      kind = "heap";
    } else if (compat_eq(node->compat, "lowmem")) {
      kind = "lowmem";
    } else if (compat_eq(node->compat, "highmem")) {
      kind = "highmem";
    } else {
      kind = "mem";
    }
    pos = 0;
    append_cstr(line, sizeof(line), &pos, "mem: ");
    append_cstr(line, sizeof(line), &pos, kind);
    append_cstr(line, sizeof(line), &pos, " base=");
    append_hex_u64(line, sizeof(line), &pos, base);
    append_cstr(line, sizeof(line), &pos, " size=");
    append_hex_u64(line, sizeof(line), &pos, size);
    if (pos + 1u < sizeof(line)) {
      line[pos++] = '\n';
    }
    line[pos < sizeof(line) ? pos : (sizeof(line) - 1u)] = '\0';
    emit(line);
  }
}
