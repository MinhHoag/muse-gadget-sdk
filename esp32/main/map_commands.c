/*
 * MAP command implementation: todo.* and pet.*.
 */
#include "map_commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Name + description pairs. Muse reads the descriptions, so they say what
 * the command does and what it returns. Keep the whole set inside the SDK's
 * 8 KB link.register budget (this is ~1.5 KB). */
typedef struct {
    const char *name;
    const char *description;
} map_command_t;

static const map_command_t k_commands[] = {
    {"todo.list", "List MAP's local task mirror; each task carries id, text, checked, pushed, createdAt, userId and synced."},
    {"todo.add", "Add a task on MAP; requires text, returns the new task id and marks it unsynced."},
    {"todo.check", "Mark a task done by id; completing a task feeds MAP's pet and returns its new mood."},
    {"todo.uncheck", "Mark a task not done by id."},
    {"todo.delete", "Delete a task by id."},
    {"todo.replace", "Replace MAP's whole task list from the TodoProj backend response; requires tasks, repairs createdAt 0."},
    {"todo.dirty", "List tasks not yet confirmed on the backend, for Muse to POST/PUT."},
    {"todo.sync_ack", "Confirm a task reached the backend: requires id and backend_id, then marks it synced."},
    {"todo.clear_synced", "Drop tasks already confirmed on the backend."},
    {"pet.status", "Report MAP's pet mood, hunger, happiness, energy and xp."},
    {"pet.feed", "Feed MAP a snack; refused when it is already full."},
    {"pet.play", "Play with MAP: costs energy and hunger, raises happiness."},
    {"pet.pet", "Give MAP a scratch; a small happiness boost."},
    {NULL, NULL},
};

static const map_command_t *find_command(const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; k_commands[i].name; i++) {
        if (strcmp(k_commands[i].name, name) == 0) return &k_commands[i];
    }
    return NULL;
}

const char *const *map_core_command_names(void) {
    static const char *names[sizeof k_commands / sizeof k_commands[0]];
    static bool built = false;
    if (!built) {
        size_t i = 0;
        for (; k_commands[i].name; i++) names[i] = k_commands[i].name;
        names[i] = NULL;
        built = true;
    }
    return names;
}

const char *map_core_command_description(const char *command) {
    const map_command_t *entry = find_command(command);
    return entry ? entry->description : NULL;
}

bool map_core_handles(const char *command) { return find_command(command) != NULL; }

/* ---- result builders --------------------------------------------------- */

static cJSON *ok_payload(cJSON *payload) {
    cJSON *result = cJSON_CreateObject();
    if (!result) {
        cJSON_Delete(payload);
        return NULL;
    }
    cJSON_AddBoolToObject(result, "ok", true);
    if (payload) cJSON_AddItemToObject(result, "payload", payload);
    return result;
}

static cJSON *fail(const char *code, const char *message) {
    cJSON *result = cJSON_CreateObject();
    if (!result) return NULL;
    cJSON_AddBoolToObject(result, "ok", false);
    cJSON *error = cJSON_CreateObject();
    if (error) {
        cJSON_AddStringToObject(error, "code", code ? code : "error");
        cJSON_AddStringToObject(error, "message", message ? message : "failed");
        cJSON_AddItemToObject(result, "error", error);
    }
    return result;
}

static int64_t now_ms(const map_core_t *core) {
    if (!core->now_us) return 1;
    int64_t us = core->now_us();
    return us > 0 ? us / 1000 : 1;
}

static void save_pet(map_core_t *core) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddNumberToObject(root, "hunger", core->pet.hunger);
    cJSON_AddNumberToObject(root, "happiness", core->pet.happiness);
    cJSON_AddNumberToObject(root, "energy", core->pet.energy);
    cJSON_AddNumberToObject(root, "xp", core->pet.xp);
    cJSON_AddNumberToObject(root, "last", (double)core->pet.last_ms);
    cJSON_AddNumberToObject(root, "happy_until", (double)core->pet.happy_until_ms);
    cJSON_AddNumberToObject(root, "born", (double)core->pet.born_ms);

    char *encoded = cJSON_PrintUnformatted(root);
    if (encoded) {
        char key[32];
        snprintf(key, sizeof key, "%spet", core->store.ns_prefix);
        core->store.kv->set(core->store.kv->ctx, key, encoded, strlen(encoded) + 1);
        cJSON_free(encoded);
    }
    cJSON_Delete(root);
}

static void load_pet(map_core_t *core) {
    char key[32], buf[512];
    size_t len = 0;
    snprintf(key, sizeof key, "%spet", core->store.ns_prefix);
    map_pet_init(&core->pet, now_ms(core));
    if (!core->store.kv->get(core->store.kv->ctx, key, buf, sizeof buf, &len) || len == 0) {
        return;
    }
    if (buf[len - 1] != '\0') {
        if (len >= sizeof buf) return;
        buf[len] = '\0';
    }
    cJSON *root = cJSON_ParseWithOpts(buf, NULL, true);
    if (!root) return;
    cJSON *hunger = cJSON_GetObjectItemCaseSensitive(root, "hunger");
    cJSON *happiness = cJSON_GetObjectItemCaseSensitive(root, "happiness");
    cJSON *energy = cJSON_GetObjectItemCaseSensitive(root, "energy");
    cJSON *xp = cJSON_GetObjectItemCaseSensitive(root, "xp");
    cJSON *last = cJSON_GetObjectItemCaseSensitive(root, "last");
    cJSON *happy = cJSON_GetObjectItemCaseSensitive(root, "happy_until");
    cJSON *born = cJSON_GetObjectItemCaseSensitive(root, "born");
    if (cJSON_IsNumber(hunger)) core->pet.hunger = hunger->valueint;
    if (cJSON_IsNumber(happiness)) core->pet.happiness = happiness->valueint;
    if (cJSON_IsNumber(energy)) core->pet.energy = energy->valueint;
    if (cJSON_IsNumber(xp)) core->pet.xp = xp->valueint;
    if (cJSON_IsNumber(last)) core->pet.last_ms = (int64_t)last->valuedouble;
    if (cJSON_IsNumber(happy)) core->pet.happy_until_ms = (int64_t)happy->valuedouble;
    if (cJSON_IsNumber(born)) core->pet.born_ms = (int64_t)born->valuedouble;
    cJSON_Delete(root);
}

static cJSON *pet_payload(const map_core_t *core, const char *event) {
    int64_t t = now_ms(core);
    map_mood_t mood = map_pet_mood(&core->pet, t);
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return NULL;
    cJSON_AddStringToObject(payload, "mood", map_mood_name(mood));
    cJSON_AddNumberToObject(payload, "hunger", core->pet.hunger);
    cJSON_AddNumberToObject(payload, "happiness", core->pet.happiness);
    cJSON_AddNumberToObject(payload, "energy", core->pet.energy);
    cJSON_AddNumberToObject(payload, "xp", core->pet.xp);
    if (event) cJSON_AddStringToObject(payload, "event", event);
    return payload;
}

/* ---- helpers ----------------------------------------------------------- */

static const char *param_string(const cJSON *params, const char *key) {
    if (!params) return NULL;
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(params, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

/* ---- handlers ---------------------------------------------------------- */

static cJSON *do_list(map_core_t *core) {
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return fail("out_of_memory", "failed to allocate result");
    cJSON_AddNumberToObject(payload, "count", (double)map_store_count(&core->store));
    cJSON *tasks = map_store_to_json(&core->store);
    if (!tasks || !cJSON_AddItemToObject(payload, "tasks", tasks)) {
        cJSON_Delete(tasks);
        cJSON_Delete(payload);
        return fail("out_of_memory", "failed to allocate task list");
    }
    return ok_payload(payload);
}

static cJSON *do_add(map_core_t *core, const cJSON *params) {
    const char *text = param_string(params, "text");
    if (!text) return fail("missing_param", "text is required");
    char err[96];
    map_task_t task;
    if (!map_store_add(&core->store, text, &task, err, sizeof err)) {
        return fail("invalid_params", err);
    }
    map_pet_task_added(&core->pet, now_ms(core));
    save_pet(core);

    cJSON *payload = cJSON_CreateObject();
    if (!payload) return fail("out_of_memory", "failed to allocate result");
    cJSON_AddStringToObject(payload, "id", task.id);
    cJSON_AddBoolToObject(payload, "synced", false);
    cJSON_AddNumberToObject(payload, "count", (double)map_store_count(&core->store));
    return ok_payload(payload);
}

static cJSON *do_check(map_core_t *core, const cJSON *params, bool checked) {
    const char *id = param_string(params, "id");
    if (!id) return fail("missing_param", "id is required");
    map_task_t task;
    if (!map_store_set_checked(&core->store, id, checked, &task)) {
        return fail("not_found", "no task with that id");
    }
    if (checked) {
        map_pet_task_completed(&core->pet, now_ms(core));
        save_pet(core);
    }
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return fail("out_of_memory", "failed to allocate result");
    cJSON_AddStringToObject(payload, "id", task.id);
    cJSON_AddBoolToObject(payload, "checked", checked);
    cJSON_AddItemToObject(payload, "pet", pet_payload(core, checked ? "fed" : NULL));
    return ok_payload(payload);
}

static cJSON *do_delete(map_core_t *core, const cJSON *params) {
    const char *id = param_string(params, "id");
    if (!id) return fail("missing_param", "id is required");
    if (!map_store_remove(&core->store, id)) {
        return fail("not_found", "no task with that id");
    }
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return fail("out_of_memory", "failed to allocate result");
    cJSON_AddStringToObject(payload, "deleted", id);
    cJSON_AddNumberToObject(payload, "count", (double)map_store_count(&core->store));
    return ok_payload(payload);
}

static cJSON *do_replace(map_core_t *core, const cJSON *params) {
    const cJSON *tasks = params
        ? cJSON_GetObjectItemCaseSensitive(params, "tasks")
        : NULL;
    if (!cJSON_IsArray(tasks)) return fail("missing_param", "tasks array is required");
    size_t kept = map_store_replace(&core->store, tasks);
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return fail("out_of_memory", "failed to allocate result");
    cJSON_AddNumberToObject(payload, "kept", (double)kept);
    cJSON_AddNumberToObject(payload, "count", (double)map_store_count(&core->store));
    return ok_payload(payload);
}

static cJSON *do_dirty(map_core_t *core) {
    cJSON *payload = cJSON_CreateObject();
    cJSON *tasks = cJSON_CreateArray();
    if (!payload || !tasks) {
        cJSON_Delete(payload);
        cJSON_Delete(tasks);
        return fail("out_of_memory", "failed to allocate result");
    }
    size_t count = 0;
    for (size_t i = 0; i < map_store_count(&core->store); i++) {
        const map_task_t *task = map_store_at(&core->store, i);
        if (!task || task->synced) continue;
        cJSON *row = map_task_to_json(task);
        if (!row || !cJSON_AddItemToArray(tasks, row)) {
            cJSON_Delete(row);
            cJSON_Delete(payload);
            cJSON_Delete(tasks);
            return fail("out_of_memory", "failed to allocate task list");
        }
        count++;
    }
    cJSON_AddNumberToObject(payload, "count", (double)count);
    cJSON_AddItemToObject(payload, "tasks", tasks);
    return ok_payload(payload);
}

/* A task pushed to the backend gets its backend id and the synced flag. */
static cJSON *do_sync_ack(map_core_t *core, const cJSON *params) {
    const char *id = param_string(params, "id");
    const char *backend = param_string(params, "backend_id");
    if (!id) return fail("missing_param", "id is required");
    if (!backend || !*backend) return fail("missing_param", "backend_id is required");

    for (size_t i = 0; i < map_store_count(&core->store); i++) {
        const map_task_t *row = map_store_at(&core->store, i);
        if (!row || strcmp(row->id, id) != 0) continue;
        if (!map_store_sync_ack(&core->store, i, backend)) {
            return fail("invalid_params",
                        "backend_id must be a non-colliding decimal id");
        }
        cJSON *payload = cJSON_CreateObject();
        cJSON_AddStringToObject(payload, "id", backend);
        cJSON_AddBoolToObject(payload, "synced", true);
        return ok_payload(payload);
    }
    return fail("not_found", "no task with that id");
}

static cJSON *do_clear_synced(map_core_t *core) {
    size_t removed = map_store_clear_synced(&core->store);
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddNumberToObject(payload, "removed", (double)removed);
    cJSON_AddNumberToObject(payload, "count", (double)map_store_count(&core->store));
    return ok_payload(payload);
}

static cJSON *do_pet_status(map_core_t *core) {
    map_pet_tick(&core->pet, now_ms(core));
    save_pet(core);
    return ok_payload(pet_payload(core, NULL));
}

static cJSON *do_pet_action(map_core_t *core, const cJSON *params, int action) {
    (void)params;
    bool did = false;
    const char *event = "idle";
    int64_t t = now_ms(core);
    switch (action) {
        case 0: did = map_pet_feed(&core->pet, t); event = "fed"; break;
        case 1: did = map_pet_play(&core->pet, t); event = "played"; break;
        default: did = map_pet_pet(&core->pet, t); event = "petted"; break;
    }
    save_pet(core);
    if (!did) {
        if (action == 0) return fail("rejected", "MAP is already full");
        return fail("rejected", "MAP is too tired to play");
    }
    return ok_payload(pet_payload(core, event));
}

/* ---- dispatch ---------------------------------------------------------- */

cJSON *map_dispatch(map_core_t *core, const char *command, const cJSON *params) {
    if (!core || !command) return fail("invalid_params", "missing command");
    if (strcmp(command, "todo.list") == 0) return do_list(core);
    if (strcmp(command, "todo.add") == 0) return do_add(core, params);
    if (strcmp(command, "todo.check") == 0) return do_check(core, params, true);
    if (strcmp(command, "todo.uncheck") == 0) return do_check(core, params, false);
    if (strcmp(command, "todo.delete") == 0) return do_delete(core, params);
    if (strcmp(command, "todo.replace") == 0) return do_replace(core, params);
    if (strcmp(command, "todo.dirty") == 0) return do_dirty(core);
    if (strcmp(command, "todo.sync_ack") == 0) return do_sync_ack(core, params);
    if (strcmp(command, "todo.clear_synced") == 0) return do_clear_synced(core);
    if (strcmp(command, "pet.status") == 0) return do_pet_status(core);
    if (strcmp(command, "pet.feed") == 0) return do_pet_action(core, params, 0);
    if (strcmp(command, "pet.play") == 0) return do_pet_action(core, params, 1);
    if (strcmp(command, "pet.pet") == 0) return do_pet_action(core, params, 2);
    return fail("unsupported", "unsupported command");
}

bool map_core_init(map_core_t *core, const map_kv_t *kv, map_clock_fn now_us,
                   const char *user_id) {
    if (!core || !kv) return false;
    memset(core, 0, sizeof(*core));
    /* The store keeps a pointer to its KV, so the KV must outlive it: keep a
     * copy inside the core instead of borrowing the caller's by pointer. */
    core->kv = *kv;
    core->now_us = now_us;
    snprintf(core->user_id, sizeof core->user_id, "%s",
             (user_id && *user_id) ? user_id : "9");
    map_store_init(&core->store, &core->kv, now_us, "map_", core->user_id);
    load_pet(core);
    return true;
}
