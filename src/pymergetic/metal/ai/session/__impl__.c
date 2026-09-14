/* pymergetic.metal.ai.session -- AI chat session with message history.
 *
 * Session table and per-session message rings are arena-allocated,
 * sized by PM_UTIL_LIMIT_C knobs. The types header defines the
 * theoretical upper bounds; the knobs set what is actually allocated.
 * No static BSS arrays beyond a few uint32_t counters. */
#include "pymergetic/metal/ai/session/__exports__.h"

#include "pymergetic/util/limits.h"
#include "pymergetic/util/mem.h"

#include <stdint.h>
#include <string.h>

#ifndef PM_METAL_AI_SESSIONS_DEFAULT
#define PM_METAL_AI_SESSIONS_DEFAULT 4u
#endif
#ifndef PM_METAL_AI_SESSIONS_HARD
#define PM_METAL_AI_SESSIONS_HARD 16u
#endif
#ifndef PM_METAL_AI_MSGS_DEFAULT
#define PM_METAL_AI_MSGS_DEFAULT 32u
#endif
#ifndef PM_METAL_AI_MSGS_HARD
#define PM_METAL_AI_MSGS_HARD 256u
#endif

struct session_slot {
    uint8_t active;
    uint32_t id;
    char name[PM_METAL_AI_SESSION_NAME_MAX];
    uint8_t persistent;
    pm_metal_ai_message_t *msgs;
    uint32_t msgs_cap;
    uint32_t head;
    uint32_t msg_count;
};

static pm_util_mem_arena_t *s_arena;
static struct session_slot *s_sessions;
static uint32_t s_sessions_cap;
static uint32_t s_session_count;
static uint32_t s_next_id = 1;

PM_UTIL_LIMIT_C(pm_ai_session_limit, pymergetic.metal.ai.session, sessions,
    PM_METAL_AI_SESSIONS_DEFAULT, PM_METAL_AI_SESSIONS_HARD, &s_session_count);
PM_UTIL_LIMIT_C(pm_ai_msg_limit, pymergetic.metal.ai.session, messages,
    PM_METAL_AI_MSGS_DEFAULT, PM_METAL_AI_MSGS_HARD, NULL);

static int32_t slot_by_id(uint32_t id) {
    uint32_t i;
    for (i = 0; i < s_sessions_cap; i++) {
        if (s_sessions[i].active && s_sessions[i].id == id)
            return (int32_t)i;
    }
    return -1;
}

static int32_t find_free_slot(void) {
    uint32_t i;
    void *grown;
    for (i = 0; i < s_sessions_cap; i++) {
        if (!s_sessions[i].active) return (int32_t)i;
    }
    if (s_arena == NULL || !PM_UTIL_LIMIT_ROOM(pm_ai_session_limit, s_session_count))
        return -1;
    grown = pm_util_limits_grow(s_arena, s_sessions, &s_sessions_cap,
        (uint32_t)sizeof(*s_sessions), &pm_ai_session_limit);
    if (grown == NULL) return -1;
    s_sessions = grown;
    return (int32_t)(s_sessions_cap - 1);
}

int32_t pm_metal_ai_session_init(pm_util_mem_arena_t *arena) {
    s_arena = arena;
    s_sessions = NULL;
    s_sessions_cap = 0;
    s_session_count = 0;
    s_next_id = 1;
    return 0;
}

void pm_metal_ai_session_deinit(void) {
    uint32_t i;
    if (s_sessions != NULL) {
        for (i = 0; i < s_sessions_cap; i++) {
            if (s_sessions[i].msgs != NULL) {
                pm_util_mem_free(s_arena, s_sessions[i].msgs);
            }
        }
        pm_util_mem_free(s_arena, s_sessions);
    }
    s_sessions = NULL;
    s_sessions_cap = 0;
    s_session_count = 0;
    s_next_id = 1;
}

int32_t pm_metal_ai_session_create(const char *name, uint32_t *out_id) {
    int32_t slot;
    if (name == NULL || out_id == NULL) return -1;
    if (name[0] == '\0') return -1;
    slot = find_free_slot();
    if (slot < 0) return -1;
    memset(&s_sessions[slot], 0, sizeof(s_sessions[slot]));
    s_sessions[slot].active = 1;
    s_sessions[slot].id = s_next_id++;
    strncpy(s_sessions[slot].name, name, sizeof(s_sessions[slot].name) - 1);
    s_sessions[slot].name[sizeof(s_sessions[slot].name) - 1] = '\0';
    *out_id = s_sessions[slot].id;
    s_session_count++;
    return 0;
}

int32_t pm_metal_ai_session_close(uint32_t id) {
    int32_t slot = slot_by_id(id);
    if (slot < 0) return -1;
    if (s_sessions[slot].msgs != NULL) {
        pm_util_mem_free(s_arena, s_sessions[slot].msgs);
    }
    memset(&s_sessions[slot], 0, sizeof(s_sessions[slot]));
    s_session_count--;
    return 0;
}

int32_t pm_metal_ai_session_find(uint32_t id, pm_metal_ai_session_t *out) {
    int32_t slot;
    if (out == NULL) return -1;
    slot = slot_by_id(id);
    if (slot < 0) return -1;
    memcpy(out->name, s_sessions[slot].name, sizeof(out->name));
    out->msg_count = s_sessions[slot].msg_count;
    out->persistent = s_sessions[slot].persistent;
    return 0;
}

int32_t pm_metal_ai_session_set_persistent(uint32_t id, uint8_t p) {
    int32_t slot = slot_by_id(id);
    if (slot < 0) return -1;
    s_sessions[slot].persistent = p ? 1 : 0;
    return 0;
}

uint32_t pm_metal_ai_session_count(void) { return s_session_count; }

int32_t pm_metal_ai_session_nth(uint32_t n, pm_metal_ai_session_t *out) {
    uint32_t i, seen = 0;
    if (out == NULL) return -1;
    for (i = 0; i < s_sessions_cap; i++) {
        if (!s_sessions[i].active) continue;
        if (seen == n) {
            memcpy(out->name, s_sessions[i].name, sizeof(out->name));
            out->msg_count = s_sessions[i].msg_count;
            out->persistent = s_sessions[i].persistent;
            return 0;
        }
        seen++;
    }
    return -1;
}

static int32_t grow_msgs(int32_t slot) {
    void *grown;
    uint32_t old_cap = s_sessions[slot].msgs_cap;
    uint32_t cap = old_cap;
    grown = pm_util_limits_grow(s_arena, s_sessions[slot].msgs, &cap,
        (uint32_t)sizeof(pm_metal_ai_message_t), &pm_ai_msg_limit);
    if (grown == NULL) return -1;
    if (old_cap > 0 && s_sessions[slot].head > 0) {
        uint32_t wrap = old_cap - s_sessions[slot].head;
        if (wrap > 0) {
            memmove((pm_metal_ai_message_t *)grown + old_cap,
                (pm_metal_ai_message_t *)grown + s_sessions[slot].head,
                wrap * sizeof(pm_metal_ai_message_t));
            s_sessions[slot].head = old_cap;
        }
    }
    s_sessions[slot].msgs = grown;
    s_sessions[slot].msgs_cap = cap;
    return 0;
}

int32_t pm_metal_ai_session_append(uint32_t id, pm_metal_ai_role_t role,
    const char *content, uint32_t content_len)
{
    int32_t slot = slot_by_id(id);
    uint32_t pos, copy_len;
    pm_metal_ai_message_t *msg;
    if (slot < 0 || content == NULL) return -1;
    if (s_sessions[slot].msg_count >= s_sessions[slot].msgs_cap) {
        if (grow_msgs(slot) != 0) {
            if (s_sessions[slot].msgs_cap == 0) return -1;
            s_sessions[slot].head =
                (s_sessions[slot].head + 1) % s_sessions[slot].msgs_cap;
            s_sessions[slot].msg_count--;
        }
    }
    pos = (s_sessions[slot].head + s_sessions[slot].msg_count)
        % s_sessions[slot].msgs_cap;
    msg = &s_sessions[slot].msgs[pos];
    memset(msg, 0, sizeof(*msg));
    msg->role = role;
    copy_len = content_len;
    if (copy_len > PM_METAL_AI_SESSION_CONTENT_MAX)
        copy_len = PM_METAL_AI_SESSION_CONTENT_MAX;
    if (copy_len > 0) memcpy(msg->content, content, copy_len);
    msg->content_len = copy_len;
    s_sessions[slot].msg_count++;
    return 0;
}

int32_t pm_metal_ai_session_message_count(uint32_t id, uint32_t *out) {
    int32_t slot = slot_by_id(id);
    if (slot < 0 || out == NULL) return -1;
    *out = s_sessions[slot].msg_count;
    return 0;
}

int32_t pm_metal_ai_session_message_nth(uint32_t id, uint32_t n,
    pm_metal_ai_message_t *out)
{
    int32_t slot = slot_by_id(id);
    uint32_t idx;
    if (slot < 0 || out == NULL || n >= s_sessions[slot].msg_count) return -1;
    idx = (s_sessions[slot].head + n) % s_sessions[slot].msgs_cap;
    *out = s_sessions[slot].msgs[idx];
    return 0;
}

int32_t pm_metal_ai_session_truncate(uint32_t id, uint32_t keep) {
    int32_t slot = slot_by_id(id);
    if (slot < 0) return -1;
    if (keep > s_sessions[slot].msg_count) keep = s_sessions[slot].msg_count;
    if (keep < s_sessions[slot].msg_count) {
        uint32_t drop = s_sessions[slot].msg_count - keep;
        s_sessions[slot].head =
            (s_sessions[slot].head + drop) % s_sessions[slot].msgs_cap;
        s_sessions[slot].msg_count = keep;
    }
    return 0;
}

int32_t pm_metal_ai_session_load(void) { return 0; }
int32_t pm_metal_ai_session_save(void) { return 0; }

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.ai.session, pm_metal_ai_session_init,
    pm_metal_ai_session_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.ai.session, pm_metal_ai_session_deinit,
    pm_metal_ai_session_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.ai.session, pm_metal_ai_session_create,
    pm_metal_ai_session_create, int32_t(const char *, uint32_t *));
PM_MOD_EXPORT_C(pymergetic.metal.ai.session, pm_metal_ai_session_close,
    pm_metal_ai_session_close, int32_t(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.ai.session, pm_metal_ai_session_find,
    pm_metal_ai_session_find, int32_t(uint32_t, pm_metal_ai_session_t *));
PM_MOD_EXPORT_C(pymergetic.metal.ai.session, pm_metal_ai_session_set_persistent,
    pm_metal_ai_session_set_persistent, int32_t(uint32_t, uint8_t));
PM_MOD_EXPORT_C(pymergetic.metal.ai.session, pm_metal_ai_session_count,
    pm_metal_ai_session_count, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.ai.session, pm_metal_ai_session_nth,
    pm_metal_ai_session_nth, int32_t(uint32_t, pm_metal_ai_session_t *));
PM_MOD_EXPORT_C(pymergetic.metal.ai.session, pm_metal_ai_session_append,
    pm_metal_ai_session_append, int32_t(uint32_t, pm_metal_ai_role_t,
    const char *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.ai.session, pm_metal_ai_session_message_count,
    pm_metal_ai_session_message_count, int32_t(uint32_t, uint32_t *));
PM_MOD_EXPORT_C(pymergetic.metal.ai.session, pm_metal_ai_session_message_nth,
    pm_metal_ai_session_message_nth, int32_t(uint32_t, uint32_t,
    pm_metal_ai_message_t *));
PM_MOD_EXPORT_C(pymergetic.metal.ai.session, pm_metal_ai_session_truncate,
    pm_metal_ai_session_truncate, int32_t(uint32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.ai.session, pm_metal_ai_session_load,
    pm_metal_ai_session_load, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.ai.session, pm_metal_ai_session_save,
    pm_metal_ai_session_save, int32_t(void));