/*
 * Engine / gather bridges only — not for fiber authors.
 * Authors use async.h: create(step,n) + await(self_h, aw).
 */
#ifndef METAL_RUNTIME_ASYNC_HOST_H_
#define METAL_RUNTIME_ASYNC_HOST_H_

#include <pymergetic/metal/runtime/async/async.h>
#include <runtime/coro/coro.h>

pm_metal_async_handle_t pm_metal_async_adopt_host_coro(pm_metal_coro_t *c);
pm_metal_status_t pm_metal_async_await_coro(pm_metal_coro_t *self, pm_metal_async_handle_t aw_h);
pm_metal_coro_t  *pm_metal_async_host_coro(pm_metal_async_handle_t h);
/** Reverse lookup: host coro/fiber pointer → handle (0 if not registered). */
pm_metal_async_handle_t pm_metal_async_handle_of(pm_metal_coro_t *c);

#endif /* METAL_RUNTIME_ASYNC_HOST_H_ */
