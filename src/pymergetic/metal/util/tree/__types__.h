/* pymergetic.metal.util.tree — the shared, colored box-drawing tree backend.
 * pymergetic.metal.boot.tree and pymergetic.metal.packages fold through this so
 * the boot banner and the CDN pack tree speak the same ANSI grammar. */
#ifndef PYMERGETIC_METAL_UTIL_TREE_TYPES_H
#define PYMERGETIC_METAL_UTIL_TREE_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ANSI styling carried by every node line: dim branch glyphs, green ok,
 * cyan sim, red FAIL, yellow warn, then reset. One place, all renderers. */
#define PM_METAL_UTIL_TREE_SGR_DIM "\033[2m"
#define PM_METAL_UTIL_TREE_SGR_OK "\033[32m"
#define PM_METAL_UTIL_TREE_SGR_SIM "\033[36m"
#define PM_METAL_UTIL_TREE_SGR_FAIL "\033[31m"
#define PM_METAL_UTIL_TREE_SGR_WARN "\033[33m"
#define PM_METAL_UTIL_TREE_SGR_RST "\033[0m"

/* Name column width for depth-0 lines with a trailing detail token, so the
 * detail values line up the way the boot tree does. */
#define PM_METAL_UTIL_TREE_NAME_PAD 13u
#define PM_METAL_UTIL_TREE_LINE 384u

/* Write a raw line (and a trailing newline). NULL prints an empty line. */
void pm_metal_util_tree_line(const char *s);

/* Render one node of a colored tree.
 *
 *   last        1 when this node is the last among its siblings (draws `--)
 *   depth       0 is the tree root; deeper levels indent one stem each
 *   parent_cont 1 when the immediately-previous level has more siblings, so
 *               this node's vertical bar keeps drawing under that parent
 *   name        node label ("?" when NULL); NULL-safe
 *   detail      trailing text, painted token-wise: `ok` green, `sim` cyan,
 *               `FAIL` red, everything else plain; NULL or "" = no detail
 *
 * `parent_cont` drives only the deepest stem; every earlier ancestor is
 * assumed to continue (it has deeper nodes along this path). This matches the
 * boot tree's depth 0..2 output exactly and generalizes to arbitrary depth,
 * so a 4-level FQN (pymergetic.wasmmod_examples.test_a.test_b.test_c) keeps
 * correct `|` bars instead of the old hardcoded three-stem cap. */
void pm_metal_util_tree_item(int last, int depth, int parent_cont, const char *name,
    const char *detail);

/* Exact-stem variant: `stems` is the full leading bar run, caller-computed, so
 * non-continuing ancestors render "    " instead of "|   ". For trees deeper
 * than the (depth, parent_cont) model can express (e.g. CDN pack FQNs). */
void pm_metal_util_tree_item_at(int last, const char *stems, const char *name,
    const char *detail);

/* Paint `detail` token-wise into dst (colored ok/sim/FAIL). Exposed for cards
 * that assemble a line by hand. detail NULL paints an empty string. */
void pm_metal_util_tree_paint_detail(char *dst, unsigned cap, const char *detail);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_UTIL_TREE_TYPES_H */
