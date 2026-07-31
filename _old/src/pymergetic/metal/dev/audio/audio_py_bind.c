/** @file
  pymergetic.metal.audio — ready / mute / volume / backend (sync control plane).
  Stream open/queue/drain stay on the C/wasm dual ABI for guests; Python
  drives the same mute/volume knobs as the shell and status-bar tray.
**/
#include <pymergetic/metal/dev/audio/audio.h>
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/py/py_obj.h>

#include "py/obj.h"

static mp_obj_t metal_audio_ready(void)
{
  return pm_metal_py_int_new((int64_t)pm_metal_audio_ready());
}
static MP_DEFINE_CONST_FUN_OBJ_0(metal_audio_ready_obj, metal_audio_ready);
PM_METAL_PY_BIND(g_py_bind_audio_ready,
                 "pymergetic.metal.audio",
                 "ready",
                 metal_audio_ready_obj,
                 PM_METAL_PY_SYNC);

static mp_obj_t metal_audio_mute(mp_obj_t on_obj)
{
  pm_metal_audio_mute((int32_t)pm_metal_py_int_get(on_obj));
  return pm_metal_py_obj_none();
}
static MP_DEFINE_CONST_FUN_OBJ_1(metal_audio_mute_obj, metal_audio_mute);
PM_METAL_PY_BIND(
  g_py_bind_audio_mute, "pymergetic.metal.audio", "mute", metal_audio_mute_obj, PM_METAL_PY_SYNC);

static mp_obj_t metal_audio_muted(void)
{
  return pm_metal_py_int_new((int64_t)pm_metal_audio_muted());
}
static MP_DEFINE_CONST_FUN_OBJ_0(metal_audio_muted_obj, metal_audio_muted);
PM_METAL_PY_BIND(g_py_bind_audio_muted,
                 "pymergetic.metal.audio",
                 "muted",
                 metal_audio_muted_obj,
                 PM_METAL_PY_SYNC);

static mp_obj_t metal_audio_volume_set(mp_obj_t pct_obj)
{
  int64_t pct;

  pct = pm_metal_py_int_get(pct_obj);
  if (pct < 0) {
    pct = 0;
  }

  if (pct > 100) {
    pct = 100;
  }

  pm_metal_audio_volume_set((uint32_t)pct);
  return pm_metal_py_obj_none();
}
static MP_DEFINE_CONST_FUN_OBJ_1(metal_audio_volume_set_obj, metal_audio_volume_set);
PM_METAL_PY_BIND(g_py_bind_audio_volume_set,
                 "pymergetic.metal.audio",
                 "volume_set",
                 metal_audio_volume_set_obj,
                 PM_METAL_PY_SYNC);

static mp_obj_t metal_audio_volume_get(void)
{
  return pm_metal_py_int_new((int64_t)pm_metal_audio_volume_get());
}
static MP_DEFINE_CONST_FUN_OBJ_0(metal_audio_volume_get_obj, metal_audio_volume_get);
PM_METAL_PY_BIND(g_py_bind_audio_volume_get,
                 "pymergetic.metal.audio",
                 "volume_get",
                 metal_audio_volume_get_obj,
                 PM_METAL_PY_SYNC);

static mp_obj_t metal_audio_backend(void)
{
  char name[32];

  if (pm_metal_audio_backend(name, (uint32_t)sizeof(name)) != 0) {
    name[0] = '\0';
  }

  return pm_metal_py_str_new(name);
}
static MP_DEFINE_CONST_FUN_OBJ_0(metal_audio_backend_obj, metal_audio_backend);
PM_METAL_PY_BIND(g_py_bind_audio_backend,
                 "pymergetic.metal.audio",
                 "backend",
                 metal_audio_backend_obj,
                 PM_METAL_PY_SYNC);
