/*
 * MAP command surface, independent of the Muse SDK plumbing.
 *
 * The SDK glue (map_bridge.c) only has to:
 *   1. advertise these names in link.register, and
 *   2. forward link.invoke params to map_dispatch().
 *
 * Every handler returns a cJSON object in the SDK's command-result shape:
 *   success: {"ok": true,  "payload": {...}}
 *   failure: {"ok": false, "error": {"code": "...", "message": "..."}}
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cJSON.h"
#include "map_pet.h"
#include "map_store.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    map_kv_t kv;      /* embedded: the store borrows a pointer to this */
    map_store_t store;
    map_pet_t pet;
    map_clock_fn now_us;
    char user_id[MAP_ID_MAX];
} map_core_t;

/* kv backs both tasks and pet state; now_us is monotonic microseconds. */
bool map_core_init(map_core_t *core, const map_kv_t *kv, map_clock_fn now_us,
                   const char *user_id);

/* Handles one command. Returns a newly allocated cJSON object (caller frees
 * with cJSON_Delete). Unknown commands return an "unsupported" error. */
cJSON *map_dispatch(map_core_t *core, const char *command, const cJSON *params);

/* True when command is one this module handles. */
bool map_core_handles(const char *command);

/* Names, for the link.register advertisement. NULL-terminated. */
const char *const *map_core_command_names(void);

/* One-line description Muse reads for command, or NULL. */
const char *map_core_command_description(const char *command);

#ifdef __cplusplus
}
#endif
