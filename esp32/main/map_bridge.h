/*
 * MAP bridge: the only ESP-IDF-specific part.
 *
 * Supplies NVS-backed storage and esp_timer to the portable core, and adapts
 * the SDK's on_ws_command()/link.register to the core's API.
 */
#pragma once

#include <stdbool.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opens the MAP NVS namespace and loads the task list and pet state.
 * Call after config_store_init() in app_run(). On failure the commands stay
 * unadvertised, which is why it returns bool. */
bool map_bridge_init(void);

/* True when this module owns the command name. */
bool map_handles_command(const char *command);

/* Handles one todo.* / pet.* command; caller owns the returned JSON. */
cJSON *map_handle_command(const char *command, const cJSON *params);

/* Names for link.register, NULL-terminated. */
const char *const *map_command_names(void);

/* One-line description Muse reads, or NULL. */
const char *map_command_description(const char *command);

#ifdef __cplusplus
}
#endif
