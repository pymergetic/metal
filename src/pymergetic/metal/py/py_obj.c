/** @file
  Host-only value facade (py_obj.h) — the only translation unit besides
  py.c/py_bind.c/py_shell.c/py_zip.c/py_guest.c/py_await.c allowed to include
  MicroPython's own headers; everything under runtime/async, guest/process,
  guest/mod, shell/shell's *_py_bind.c files goes through this instead.
**/
#include <string.h>

#include <pymergetic/metal/py/py_obj.h>

#include "py/obj.h"
#include "py/runtime.h"

pm_metal_py_obj_t pm_metal_py_obj_none(void)
{
  return (pm_metal_py_obj_t)mp_const_none;
}

bool pm_metal_py_obj_is_none(pm_metal_py_obj_t o)
{
  return (mp_obj_t)o == mp_const_none;
}

pm_metal_py_obj_t pm_metal_py_int_new(int64_t v)
{
  return (pm_metal_py_obj_t)mp_obj_new_int((mp_int_t)v);
}

int64_t pm_metal_py_int_get(pm_metal_py_obj_t o)
{
  return (int64_t)mp_obj_get_int((mp_obj_t)o);
}

pm_metal_py_obj_t pm_metal_py_str_new(const char *s)
{
  return (pm_metal_py_obj_t)mp_obj_new_str(s, strlen(s));
}

pm_metal_py_obj_t pm_metal_py_bytes_new(const uint8_t *data, size_t len)
{
  return (pm_metal_py_obj_t)mp_obj_new_bytes(data, len);
}

bool pm_metal_py_buf_get(pm_metal_py_obj_t o, const uint8_t **out, size_t *out_len)
{
  mp_buffer_info_t info;

  mp_get_buffer_raise((mp_obj_t)o, &info, MP_BUFFER_READ);
  *out     = (const uint8_t *)info.buf;
  *out_len = (size_t)info.len;
  return true;
}

pm_metal_py_obj_t pm_metal_py_list_new(void)
{
  return (pm_metal_py_obj_t)mp_obj_new_list(0, NULL);
}

void pm_metal_py_list_append(pm_metal_py_obj_t list, pm_metal_py_obj_t item)
{
  mp_obj_list_append((mp_obj_t)list, (mp_obj_t)item);
}

int pm_metal_py_obj_to_str(pm_metal_py_obj_t o, char *buf, size_t cap)
{
  mp_obj_t    str_obj = mp_call_function_1(MP_OBJ_FROM_PTR(&mp_type_str), (mp_obj_t)o);
  const char *s       = mp_obj_str_get_str(str_obj);
  size_t      len     = strlen(s);

  if (cap == 0u) {
    return -1;
  }
  if (len >= cap) {
    memcpy(buf, s, cap - 1u);
    buf[cap - 1u] = '\0';
    return -1;
  }
  memcpy(buf, s, len + 1u);
  return 0;
}

pm_metal_py_obj_t pm_metal_py_dict_new(size_t n_hint)
{
  return (pm_metal_py_obj_t)mp_obj_new_dict(n_hint);
}

void pm_metal_py_dict_set_str(pm_metal_py_obj_t d, const char *key, pm_metal_py_obj_t val)
{
  mp_obj_dict_store((mp_obj_t)d, MP_OBJ_NEW_QSTR(qstr_from_str(key)), (mp_obj_t)val);
}

pm_metal_py_obj_t pm_metal_py_tuple_new(size_t n, const pm_metal_py_obj_t *items)
{
  return (pm_metal_py_obj_t)mp_obj_new_tuple(n, (const mp_obj_t *)items);
}

void pm_metal_py_raise_type_error(const char *msg)
{
  mp_raise_TypeError(MP_ERROR_TEXT(msg));
}

void pm_metal_py_raise_value_error(const char *msg)
{
  mp_raise_ValueError(MP_ERROR_TEXT(msg));
}

void pm_metal_py_raise_runtime_error(const char *msg)
{
  mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT(msg));
}
