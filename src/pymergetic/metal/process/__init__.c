/*
 * pymergetic.metal.process — intent-root table over async handles.
 */
#include <pymergetic/metal/process/__init__.h>
#include <pymergetic/metal/async/handle.h>
#include <pymergetic/metal/async/task.h>

#include <string.h>

typedef struct {
    uint8_t used;
    uint8_t mode;
    uint32_t async_handle;
    int32_t exit_code;
    char tag[PM_METAL_PROCESS_TAG_MAX];
    pm_metal_process_teardown_fn teardown;
    void *teardown_user;
} proc_slot_t;

static proc_slot_t g_procs[PM_METAL_PROCESS_MAX];
static uint32_t g_current;
static int32_t g_shutting_down;

static void tag_copy(char *dst, const char *tag)
{
    size_t i;
    if (tag == NULL) {
        dst[0] = '\0';
        return;
    }
    for (i = 0; i + 1u < PM_METAL_PROCESS_TAG_MAX && tag[i]; i++) {
        dst[i] = tag[i];
    }
    dst[i] = '\0';
}

static uint32_t alloc_pid(void)
{
    uint32_t i;
    for (i = 1; i < PM_METAL_PROCESS_MAX; i++) {
        if (!g_procs[i].used) {
            memset(&g_procs[i], 0, sizeof(g_procs[i]));
            g_procs[i].used = 1;
            return i;
        }
    }
    return 0;
}

void pm_metal_process_set_shutting_down(int32_t on)
{
    g_shutting_down = on ? 1 : 0;
}

int32_t pm_metal_process_shutting_down(void)
{
    return g_shutting_down;
}

uint32_t pm_metal_process_current(void)
{
    return g_current;
}

uint32_t pm_metal_process_crown(uint32_t async_handle, pm_metal_process_mode_t mode,
                                const char *tag, pm_metal_process_teardown_fn teardown,
                                void *teardown_user)
{
    uint32_t pid;
    if (g_shutting_down) {
        return 0;
    }
    pid = alloc_pid();
    if (pid == 0) {
        return 0;
    }
    g_procs[pid].async_handle = async_handle;
    g_procs[pid].mode = (uint8_t)mode;
    g_procs[pid].teardown = teardown;
    g_procs[pid].teardown_user = teardown_user;
    tag_copy(g_procs[pid].tag, tag);
    if (g_current == 0 && mode == PM_METAL_PROCESS_MODE_FG) {
        g_current = pid;
    }
    return pid;
}

uint32_t pm_metal_process_spawn(pm_metal_async_step_fn_t step, uint32_t state_bytes,
                                pm_metal_async_prio_t prio, pm_metal_process_mode_t mode,
                                const char *tag, pm_metal_process_teardown_fn teardown,
                                void *teardown_user)
{
    uint32_t h;
    if (g_shutting_down || step == NULL) {
        return 0;
    }
    h = pm_metal_async_spawn(step, state_bytes, prio);
    if (h == 0) {
        return 0;
    }
    return pm_metal_process_crown(h, mode, tag, teardown, teardown_user);
}

int32_t pm_metal_process_quit(uint32_t pid, int32_t code)
{
    proc_slot_t *p;
    uint32_t h;
    pm_metal_process_teardown_fn td;
    void *user;

    if (pid == 0) {
        pid = g_current;
    }
    if (pid == 0 || pid >= PM_METAL_PROCESS_MAX || !g_procs[pid].used) {
        return -1;
    }
    p = &g_procs[pid];
    p->exit_code = code;
    h = p->async_handle;
    td = p->teardown;
    user = p->teardown_user;
    if (g_current == pid) {
        g_current = 0;
    }
    p->async_handle = 0;
    p->teardown = NULL;
    p->teardown_user = NULL;
    if (td != NULL) {
        td(pid, user);
    }
    if (h != 0) {
        pm_metal_async_cancel_tree(h);
    }
    memset(p, 0, sizeof(*p));
    return 0;
}

uint32_t pm_metal_process_list(pm_metal_process_info_t *infos, uint32_t max)
{
    uint32_t i, n = 0;
    if (infos == NULL || max == 0) {
        return 0;
    }
    for (i = 1; i < PM_METAL_PROCESS_MAX && n < max; i++) {
        if (!g_procs[i].used) {
            continue;
        }
        infos[n].pid = i;
        infos[n].async_handle = g_procs[i].async_handle;
        infos[n].mode = (pm_metal_process_mode_t)g_procs[i].mode;
        infos[n].exit_code = g_procs[i].exit_code;
        memcpy(infos[n].tag, g_procs[i].tag, PM_METAL_PROCESS_TAG_MAX);
        n++;
    }
    return n;
}

uint32_t pm_metal_process_quit_all(int32_t code)
{
    uint32_t i, n = 0;
    for (i = 1; i < PM_METAL_PROCESS_MAX; i++) {
        if (!g_procs[i].used) {
            continue;
        }
        if (pm_metal_process_quit(i, code) == 0) {
            n++;
        }
    }
    return n;
}
