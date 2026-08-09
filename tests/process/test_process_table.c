/*
 * Host unit test: process table crown/list/quit/quit_all + shutting_down gate.
 * Build: make -C tests/process
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/process/__init__.h>

/* Stubs — table-only processes (async_handle 0). */
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

static int g_teardown_hits;

static void td(uint32_t pid, void *user)
{
    (void)user;
    assert(pid != 0);
    g_teardown_hits++;
}

int main(void)
{
    pm_metal_process_info_t infos[8];
    uint32_t a, b, n;

    a = pm_metal_process_crown(0, PM_METAL_PROCESS_MODE_DAEMON, "sshd", td, NULL);
    b = pm_metal_process_crown(0, PM_METAL_PROCESS_MODE_DAEMON, "httpd", td, NULL);
    assert(a != 0 && b != 0 && a != b);

    n = pm_metal_process_list(infos, 8);
    assert(n == 2);

    assert(pm_metal_process_quit(a, 0) == 0);
    assert(g_teardown_hits == 1);
    n = pm_metal_process_list(infos, 8);
    assert(n == 1);
    assert(strcmp(infos[0].tag, "httpd") == 0);

    pm_metal_process_set_shutting_down(1);
    assert(pm_metal_process_crown(0, PM_METAL_PROCESS_MODE_BG, "nope", NULL, NULL) == 0);
    pm_metal_process_set_shutting_down(0);

    assert(pm_metal_process_quit_all(0) == 1);
    assert(g_teardown_hits == 2);
    assert(pm_metal_process_list(infos, 8) == 0);

    puts("process table ok");
    return 0;
}
