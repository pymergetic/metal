/*
 * Host unit: unboot quits all procs + shutting_down; shutdown marks dead.
 */
#include <assert.h>
#include <stdio.h>

#include <pymergetic/metal/boot/tree.h>
#include <pymergetic/metal/boot/unboot.h>
#include <pymergetic/metal/process/__init__.h>

void pm_metal_async_cancel_tree(uint32_t h)
{
    (void)h;
}

uint32_t pm_metal_async_spawn(void *step, uint32_t state_bytes, int prio)
{
    (void)step;
    (void)state_bytes;
    (void)prio;
    return 0;
}

/* Tree UX — stubbed for table-only host link. */
void pm_metal_boot_emit(const char *line)
{
    (void)line;
}
void pm_metal_boot_tree_reset(void) {}
void pm_metal_boot_tree_enter(const char *name)
{
    (void)name;
}
void pm_metal_boot_tree_item(const char *name, pm_metal_boot_tree_status_t st, const char *detail)
{
    (void)name;
    (void)st;
    (void)detail;
}
void pm_metal_boot_tree_leave(void) {}
void pm_metal_boot_tree_dead(void) {}
void pm_metal_boot_dead_art(const char *version, const char *cpu)
{
    (void)version;
    (void)cpu;
}

static int g_td;

static void td(uint32_t pid, void *user)
{
    (void)pid;
    (void)user;
    g_td++;
}

int main(void)
{
    uint32_t a = pm_metal_process_crown(0, PM_METAL_PROCESS_MODE_DAEMON, "sshd", td, NULL);
    uint32_t b = pm_metal_process_crown(0, PM_METAL_PROCESS_MODE_DAEMON, "httpd", td, NULL);
    assert(a && b);

    assert(pm_metal_boot_unboot() == 0);
    assert(pm_metal_boot_shutting_down() != 0);
    assert(g_td == 2);
    {
        pm_metal_process_info_t infos[4];
        assert(pm_metal_process_list(infos, 4) == 0);
    }
    assert(pm_metal_process_crown(0, PM_METAL_PROCESS_MODE_BG, "nope", NULL, NULL) == 0);

    pm_metal_boot_clear_dead();
    assert(pm_metal_boot_shutting_down() == 0);

    (void)pm_metal_process_crown(0, PM_METAL_PROCESS_MODE_DAEMON, "sshd", td, NULL);
    assert(pm_metal_boot_shutdown() == 0);
    assert(pm_metal_boot_is_dead() != 0);
    assert(g_td == 3);

    puts("unboot ok");
    return 0;
}
