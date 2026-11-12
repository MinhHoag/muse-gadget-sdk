/*
 * MAP task store implementation. Portable C11 + cJSON, no ESP-IDF headers.
 */
#include "map_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- helpers ---------------------------------------------------------- */

static void copy_str(char *dst, size_t cap, const char *src) {
    if (cap == 0) return;
    size_t n = src ? strlen(src) : 0;
    if (n >= cap) n = cap - 1;
    if (n) memcpy(dst, src, n);
    dst[n] = '\0';
}

/* Trim and collapse internal whitespace runs; returns the length. */
static size_t clean_text(char *out, size_t cap, const char *in) {
    size_t n = 0;
    bool pending_space = false;
    for (const char *p = in; p && *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            pending_space = n > 0;
            continue;
        }
        if (pending_space) {
            if (n + 1 >= cap) break;
            out[n++] = ' ';
            pending_space = false;
        }
        if (n + 1 >= cap) break;
        out[n++] = (char)c;
    }
    out[n] = '\0';
    return n;
}

static bool text_equal(const char *a, const char *b) {
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return *a == *b;
}

static int64_t store_now_ms(map_store_t *st) {
    if (!st->now_us) return 1;
    int64_t us = st->now_us();
    return us > 0 ? us / 1000 : 1;
}

static const map_task_t *find_mut(const map_store_t *st, const char *id,
                                  size_t *index_out) {
    for (size_t i = 0; i < st->count; i++) {
        if (strcmp(st->tasks[i].id, id) == 0) {
            if (index_out) *index_out = i;
            return &st->tasks[i];
        }
    }
    return NULL;
}

/* A task id is a decimal string of 1..MAP_NUM_ID_MAX digits. */
static bool valid_id_text(const char *id) {
    if (!id || !*id) return false;
    size_t n = 0;
    for (const char *p = id; *p; p++) {
        if (*p < '0' || *p > '9') return false;
        if (++n > MAP_NUM_ID_MAX) return false;
    }
    return true;
}

static bool id_to_int(const char *id, int *out) {
    if (!valid_id_text(id)) return false;
    long v = strtol(id, NULL, 10);
    if (v <= 0 || v > 0x7fffffffL) return false;
    *out = (int)v;
    return true;
}

/* ---- persistence ------------------------------------------------------- */

static bool save_json(map_store_t *st, const char *key, const cJSON *root) {
    char *encoded = cJSON_PrintUnformatted(root);
    if (!encoded) return false;
    bool ok = st->kv->set(st->kv->ctx, key, encoded, strlen(encoded) + 1);
    cJSON_free(encoded);
    return ok;
}

static cJSON *load_json(map_store_t *st, const char *key) {
    const size_t capacity = 8192;
    char *buf = malloc(capacity);
    if (!buf) return NULL;

    size_t len = 0;
    cJSON *root = NULL;
    if (st->kv->get(st->kv->ctx, key, buf, capacity, &len) && len > 0 && len <= capacity) {
        if (buf[len - 1] == '\0') {
            root = cJSON_ParseWithOpts(buf, NULL, true);
        } else if (len < capacity) {
            buf[len] = '\0';
            root = cJSON_ParseWithOpts(buf, NULL, true);
        }
    }
    free(buf);
    return root;
}

static void load_names(map_store_t *st, const char *tasks_key,
                       const char *next_key) {
    cJSON *root = load_json(st, tasks_key);
    if (root) {
        map_store_load(st, root);   /* keeps only valid rows, preserves synced */
        cJSON_Delete(root);
    }
    cJSON *next = load_json(st, next_key);
    if (cJSON_IsNumber(next)) {
        int stored = next->valueint;
        if (stored > st->next_id) st->next_id = stored;
    }
    cJSON_Delete(next);
}

bool map_store_save(map_store_t *st) {
    char tasks_key[32], next_key[32];
    snprintf(tasks_key, sizeof tasks_key, "%stasks", st->ns_prefix);
    snprintf(next_key, sizeof next_key, "%snext", st->ns_prefix);

    cJSON *root = cJSON_CreateArray();
    if (!root) return false;
    for (size_t i = 0; i < st->count; i++) {
        cJSON *row = map_task_to_json(&st->tasks[i]);
        if (!row || !cJSON_AddItemToArray(root, row)) {
            cJSON_Delete(row);
            cJSON_Delete(root);
            return false;
        }
    }
    bool ok = save_json(st, tasks_key, root);
    cJSON_Delete(root);
    if (!ok) return false;

    cJSON *next = cJSON_CreateNumber(st->next_id);
    if (!next) return false;
    ok = save_json(st, next_key, next);
    cJSON_Delete(next);
    return ok;
}

void map_store_init(map_store_t *st, const map_kv_t *kv, map_clock_fn now_us,
                    const char *ns_prefix, const char *user_id) {
    memset(st, 0, sizeof(*st));
    st->kv = kv;
    st->now_us = now_us;
    st->next_id = 1;
    copy_str(st->ns_prefix, sizeof st->ns_prefix,
             (ns_prefix && *ns_prefix) ? ns_prefix : "map_");
    copy_str(st->user_id, sizeof st->user_id, (user_id && *user_id) ? user_id : "9");

    char tasks_key[32], next_key[32];
    snprintf(tasks_key, sizeof tasks_key, "%stasks", st->ns_prefix);
    snprintf(next_key, sizeof next_key, "%snext", st->ns_prefix);
    load_names(st, tasks_key, next_key);
}

/* ---- queries ----------------------------------------------------------- */

size_t map_store_count(const map_store_t *st) { return st->count; }

const map_task_t *map_store_at(const map_store_t *st, size_t index) {
    return index < st->count ? &st->tasks[index] : NULL;
}

map_task_t *map_store_at_mut(map_store_t *st, size_t index) {
    return index < st->count ? &st->tasks[index] : NULL;
}

const map_task_t *map_store_find(const map_store_t *st, const char *id) {
    if (!id) return NULL;
    return find_mut(st, id, NULL);
}

bool map_store_sync_ack(map_store_t *st, size_t index, const char *backend_id) {
    if (index >= st->count) return false;
    if (!valid_id_text(backend_id)) return false;
    /* The backend id must not collide with a different row. */
    for (size_t i = 0; i < st->count; i++) {
        if (i != index && strcmp(st->tasks[i].id, backend_id) == 0) return false;
    }
    copy_str(st->tasks[index].id, sizeof st->tasks[index].id, backend_id);
    st->tasks[index].synced = true;
    map_store_save(st);
    return true;
}

size_t map_store_clear_synced(map_store_t *st) {
    size_t removed = 0, kept = 0;
    for (size_t i = 0; i < st->count; i++) {
        if (st->tasks[i].synced) {
            removed++;
            continue;
        }
        if (kept != i) st->tasks[kept] = st->tasks[i];
        kept++;
    }
    st->count = kept;
    map_store_save(st);
    return removed;
}

/* ---- mutations --------------------------------------------------------- */

bool map_store_add(map_store_t *st, const char *text, map_task_t *out,
                   char *err, size_t err_cap) {
    if (err && err_cap) err[0] = '\0';
    char cleaned[MAP_TEXT_MAX];
    size_t len = clean_text(cleaned, sizeof cleaned, text);
    if (len == 0) {
        if (err) snprintf(err, err_cap, "text is required");
        return false;
    }
    for (size_t i = 0; i < st->count; i++) {
        if (text_equal(st->tasks[i].text, cleaned)) {
            if (err) snprintf(err, err_cap, "a task with that text already exists");
            return false;
        }
    }
    if (st->count >= MAP_MAX_TASKS) {
        if (err) snprintf(err, err_cap, "the task list is full");
        return false;
    }
    if (st->next_id <= 0 || st->next_id > 0x7fffffff) {
        if (err) snprintf(err, err_cap, "the task id space is exhausted");
        return false;
    }

    map_task_t *t = &st->tasks[st->count];
    memset(t, 0, sizeof(*t));
    snprintf(t->id, sizeof t->id, "%d", st->next_id++);
    copy_str(t->text, sizeof t->text, cleaned);
    t->checked = false;
    t->pushed = false;
    t->created_at = store_now_ms(st);
    copy_str(t->user_id, sizeof t->user_id, st->user_id);
    t->synced = false;
    st->count++;
    if (out) *out = *t;
    map_store_save(st);
    return true;
}

bool map_store_set_checked(map_store_t *st, const char *id, bool checked,
                           map_task_t *out) {
    size_t index = 0;
    if (!find_mut(st, id, &index)) return false;
    st->tasks[index].checked = checked;
    if (out) *out = st->tasks[index];
    map_store_save(st);
    return true;
}

bool map_store_set_text(map_store_t *st, const char *id, const char *text,
                        map_task_t *out, char *err, size_t err_cap) {
    if (err && err_cap) err[0] = '\0';
    size_t index = 0;
    if (!find_mut(st, id, &index)) {
        if (err) snprintf(err, err_cap, "no task with that id");
        return false;
    }
    char cleaned[MAP_TEXT_MAX];
    if (clean_text(cleaned, sizeof cleaned, text) == 0) {
        if (err) snprintf(err, err_cap, "text is required");
        return false;
    }
    if (strcmp(st->tasks[index].text, cleaned) != 0) {
        for (size_t i = 0; i < st->count; i++) {
            if (i != index && text_equal(st->tasks[i].text, cleaned)) {
                if (err) snprintf(err, err_cap, "a task with that text already exists");
                return false;
            }
        }
    }
    copy_str(st->tasks[index].text, sizeof st->tasks[index].text, cleaned);
    st->tasks[index].synced = false;
    if (out) *out = st->tasks[index];
    map_store_save(st);
    return true;
}

bool map_store_remove(map_store_t *st, const char *id) {
    size_t index = 0;
    if (!find_mut(st, id, &index)) return false;
    for (size_t i = index; i + 1 < st->count; i++) st->tasks[i] = st->tasks[i + 1];
    st->count--;
    map_store_save(st);
    return true;
}

/* Shared row ingestion. `from_backend` marks rows synced (the server is the
 * source) and ignores a stored flag; a local load keeps the stored flag so
 * offline edits stay queued across a reboot. */
static size_t apply_rows(map_store_t *st, const cJSON *tasks,
                         bool from_backend) {
    if (!cJSON_IsArray(tasks)) return st->count;
    size_t kept = 0;
    int max_id = 0;
    const cJSON *row;
    cJSON_ArrayForEach(row, tasks) {
        if (kept >= MAP_MAX_TASKS) break;
        if (!cJSON_IsObject(row)) continue;
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(row, "id");
        const cJSON *text = cJSON_GetObjectItemCaseSensitive(row, "text");
        const cJSON *checked = cJSON_GetObjectItemCaseSensitive(row, "checked");
        if (!cJSON_IsString(id) || !valid_id_text(id->valuestring)) continue;
        if (!cJSON_IsString(text) || !text->valuestring || !*text->valuestring) continue;
        if (!cJSON_IsBool(checked)) continue;

        map_task_t *t = &st->tasks[kept];
        memset(t, 0, sizeof(*t));
        copy_str(t->id, sizeof t->id, id->valuestring);
        clean_text(t->text, sizeof t->text, text->valuestring);

        const cJSON *pushed = cJSON_GetObjectItemCaseSensitive(row, "pushed");
        t->checked = cJSON_IsTrue(checked);
        t->pushed = cJSON_IsTrue(pushed);

        const cJSON *created = cJSON_GetObjectItemCaseSensitive(row, "createdAt");
        int64_t when = cJSON_IsNumber(created) ? (int64_t)created->valuedouble : 0;
        if (when <= 0) when = store_now_ms(st);   /* repair createdAt:0 rows */
        t->created_at = when;

        const cJSON *uid = cJSON_GetObjectItemCaseSensitive(row, "userId");
        if (cJSON_IsString(uid) && uid->valuestring && *uid->valuestring) {
            copy_str(t->user_id, sizeof t->user_id, uid->valuestring);
        } else {
            copy_str(t->user_id, sizeof t->user_id, st->user_id);
        }

        if (from_backend) {
            t->synced = true;
        } else {
            const cJSON *synced = cJSON_GetObjectItemCaseSensitive(row, "synced");
            t->synced = cJSON_IsTrue(synced);
        }

        int numeric = 0;
        if (id_to_int(t->id, &numeric) && numeric > max_id) max_id = numeric;
        kept++;
    }
    st->count = kept;
    if (max_id >= st->next_id) st->next_id = max_id + 1;
    map_store_save(st);
    return kept;
}

size_t map_store_load(map_store_t *st, const cJSON *tasks) {
    return apply_rows(st, tasks, false);
}

size_t map_store_replace(map_store_t *st, const cJSON *tasks) {
    return apply_rows(st, tasks, true);
}

/* ---- JSON -------------------------------------------------------------- */

cJSON *map_task_to_json(const map_task_t *task) {
    if (!task) return NULL;
    cJSON *row = cJSON_CreateObject();
    if (!row) return NULL;
    if (!cJSON_AddStringToObject(row, "id", task->id) ||
        !cJSON_AddStringToObject(row, "text", task->text) ||
        !cJSON_AddBoolToObject(row, "checked", task->checked)) {
        cJSON_Delete(row);
        return NULL;
    }
    /* pushed is sent when true, matching how the backend rows look. */
    if (task->pushed && !cJSON_AddBoolToObject(row, "pushed", true)) {
        cJSON_Delete(row);
        return NULL;
    }
    /* The web app stores createdAt as a number (ms since epoch) and sorts on
     * it, so keep the JSON type numeric. */
    if (!cJSON_AddNumberToObject(row, "createdAt", (double)task->created_at)) {
        cJSON_Delete(row);
        return NULL;
    }
    if (!cJSON_AddStringToObject(row, "userId", task->user_id)) {
        cJSON_Delete(row);
        return NULL;
    }
    /* Local-only, never written to the backend: tells Muse where the row
     * already exists. */
    cJSON_AddBoolToObject(row, "synced", task->synced);
    return row;
}

cJSON *map_store_to_json(const map_store_t *st) {
    cJSON *array = cJSON_CreateArray();
    if (!array) return NULL;
    for (size_t i = 0; i < st->count; i++) {
        cJSON *row = map_task_to_json(&st->tasks[i]);
        if (!row || !cJSON_AddItemToArray(array, row)) {
            cJSON_Delete(row);
            cJSON_Delete(array);
            return NULL;
        }
    }
    return array;
}
