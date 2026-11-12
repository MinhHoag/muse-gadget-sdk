/*
 * MAP task store.
 *
 * A small, fixed-capacity mirror of the TodoProj backend (MockAPI /user/9).
 * It is board- and platform-independent: storage and time are injected, so
 * the same code runs on the ESP32 (NVS + esp_timer) and on a PC (fake KV +
 * fake clock) for host tests.
 *
 * Task shape matches what the web app reads and writes:
 *   {id, text, checked, pushed, createdAt, userId}
 * plus one local-only field, `synced`, that is never sent to the backend.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAP_MAX_TASKS    48
#define MAP_TEXT_MAX     160
#define MAP_ID_MAX       32
#define MAP_NUM_ID_MAX   20   /* decimal string, 64-bit */

/* ---- Injected storage -------------------------------------------------
 * One namespace, string keys, byte values. On the ESP32 this wraps NVS; on
 * the host it is an in-memory map. Return false on any storage failure.
 */
typedef struct {
    void *ctx;
    bool (*get)(void *ctx, const char *key, char *out, size_t cap, size_t *len);
    bool (*set)(void *ctx, const char *key, const void *data, size_t len);
} map_kv_t;

/* Monotonic microseconds, same contract as esp_timer_get_time(). */
typedef int64_t (*map_clock_fn)(void);

typedef struct {
    char id[MAP_ID_MAX];  /* decimal string: gadget id, then backend id */
    char text[MAP_TEXT_MAX];
    bool checked;
    bool pushed;
    int64_t created_at;   /* milliseconds; never 0 */
    char user_id[MAP_ID_MAX];  /* "9" */
    bool synced;          /* local only: true once the backend has the row */
} map_task_t;

typedef struct {
    const map_kv_t *kv;
    map_clock_fn now_us;
    char ns_prefix[8];    /* "map_", or "test_" under host tests */
    char user_id[MAP_ID_MAX];
    map_task_t tasks[MAP_MAX_TASKS];
    size_t count;
    int next_id;
} map_store_t;

/* Load from storage; never fails hard, an unreadable/corrupt blob starts
 * empty (the backend remains the source of truth). */
void map_store_init(map_store_t *st, const map_kv_t *kv, map_clock_fn now_us,
                    const char *ns_prefix, const char *user_id);

/* Persist the whole array. Returns false if storage rejected it. */
bool map_store_save(map_store_t *st);

size_t map_store_count(const map_store_t *st);
const map_task_t *map_store_at(const map_store_t *st, size_t index);
/* Mutable row access for the modules that own a transition (sync ack). */
map_task_t *map_store_at_mut(map_store_t *st, size_t index);
const map_task_t *map_store_find(const map_store_t *st, const char *id);

/* The backend accepted a task: replace its gadget id with the backend id and
 * mark it synced. Returns false for a bad index, a non-decimal id, or a
 * collision with another task's id. */
bool map_store_sync_ack(map_store_t *st, size_t index, const char *backend_id);

/* Drop tasks already confirmed on the backend (tidy-up after a sync). */
size_t map_store_clear_synced(map_store_t *st);

/* Add a task. text must be non-empty, <= MAP_TEXT_MAX, and not duplicate an
 * existing task's text. createdAt is always a real clock value. */
bool map_store_add(map_store_t *st, const char *text, map_task_t *out,
                   char *err, size_t err_cap);

bool map_store_set_checked(map_store_t *st, const char *id, bool checked,
                           map_task_t *out);
bool map_store_set_text(map_store_t *st, const char *id, const char *text,
                        map_task_t *out, char *err, size_t err_cap);
bool map_store_remove(map_store_t *st, const char *id);

/* Load MAP's own stored list (preserves each row's `synced` flag). This is
 * what boot uses: an offline-created task must stay dirty across a reboot. */
size_t map_store_load(map_store_t *st, const cJSON *tasks);

/* Replace the whole task list from a backend snapshot (GET /user/9).
 * Rows are accepted only when they have a decimal id, non-empty text and a
 * checked boolean; createdAt 0 is repaired to now. Every row becomes synced,
 * because the backend is where it came from. Returns the number kept. */
size_t map_store_replace(map_store_t *st, const cJSON *tasks);

/* Serialise to the backend's shape, for todo.list results and for Muse to
 * PUT to /user/9/tasks/{id}. Each row also carries a local-only `synced`
 * flag so Muse knows whether the id is a backend id or a gadget id. */
cJSON *map_store_to_json(const map_store_t *st);
cJSON *map_task_to_json(const map_task_t *task);

#ifdef __cplusplus
}
#endif
