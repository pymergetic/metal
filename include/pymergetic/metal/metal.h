/*
 * Mod-facing umbrella — wasm32-wasip1 guests.
 *
 * impl: none — umbrella re-export (see #includes)
 */
#ifndef PYMERGETIC_METAL_METAL_H_
#define PYMERGETIC_METAL_METAL_H_

#include "pymergetic/metal/dev/gfx/gfx.h" /* IWYU pragma: export */
#include "pymergetic/metal/shell/ui/ui.h" /* IWYU pragma: export */
#include "pymergetic/metal/shell/shell/shell.h" /* IWYU pragma: export */
#include "pymergetic/metal/runtime/async/async.h" /* IWYU pragma: export */
#include "pymergetic/metal/dev/input/input.h" /* IWYU pragma: export */
#include "pymergetic/metal/fs/fs.h" /* IWYU pragma: export */
#include "pymergetic/metal/dev/blk/blk.h" /* IWYU pragma: export */
#include "pymergetic/metal/dev/audio/audio.h" /* IWYU pragma: export */
#include "pymergetic/metal/dev/net/net.h" /* IWYU pragma: export */
#include "pymergetic/metal/dev/net/ping.h" /* IWYU pragma: export */
#include "pymergetic/metal/dev/net/http.h" /* IWYU pragma: export */
#include "pymergetic/metal/dev/net/tftp.h" /* IWYU pragma: export */
#if !defined(__wasm__)
#include "pymergetic/metal/dev/net/tls.h" /* IWYU pragma: export */
#endif
#include "pymergetic/metal/dev/stream/stream.h" /* IWYU pragma: export */
#include "pymergetic/metal/shell/lifecycle/lifecycle.h" /* IWYU pragma: export */
#include "pymergetic/metal/shell/hwinfo/hwinfo.h" /* IWYU pragma: export */
#include "pymergetic/metal/dev/random/random.h" /* IWYU pragma: export */
#include "pymergetic/metal/util/arena.h" /* IWYU pragma: export */
#include "pymergetic/metal/util/log.h" /* IWYU pragma: export */
#include "pymergetic/metal/util/lz4.h" /* IWYU pragma: export */
#include "pymergetic/metal/util/tar.h" /* IWYU pragma: export */
#include "pymergetic/metal/util/crypto.h" /* IWYU pragma: export */
#include "pymergetic/metal/util/ascii.h" /* IWYU pragma: export */
#include "pymergetic/metal/util/size.h" /* IWYU pragma: export */
#include "pymergetic/metal/util/ip.h" /* IWYU pragma: export */
#include "pymergetic/metal/host/host.h" /* IWYU pragma: export */

#endif /* PYMERGETIC_METAL_METAL_H_ */
