/*
 * MAP bridge implementation. ESP-IDF only; excluded from host builds.
 */
#include "map_bridge.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

#include "map_commands.h"

static const char *TAG = "map.bridge";

#define MAP_NVS_NAMESPACE "map"

#ifndef CONFIG_MAP_USER_ID
#define CONFIG_MAP_USER_ID "9"
#endif
#define MAP_USER_ID CONFIG_MAP_USER_ID

static nvs_handle_t s_handle;
static bool s_open;
static map_core_t s_core;   /* ~12 KB, so it lives in .bss not on a stack */

static bool kv_get(void *ctx, const char *key, char *out, size_t cap,
                   size_t *len) {
    nvs_handle_t handle = *(nvs_handle_t *)ctx;
    size_t size = cap;
    esp_err_t err = nvs_get_blob(handle, key, out, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_INVALID_LENGTH) {
        return false;
    }
    if (err != ESP_OK) return false;
    *len = size;
    return true;
}

static bool kv_set(void *ctx, const char *key, const void *data, size_t len) {
    nvs_handle_t handle = *(nvs_handle_t *)ctx;
    if (nvs_set_blob(handle, key, data, len) != ESP_OK) return false;
    return nvs_commit(handle) == ESP_OK;
}

bool map_bridge_init(void) {
    if (s_open) return true;
    esp_err_t err = nvs_open(MAP_NVS_NAMESPACE, NVS_READWRITE, &s_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(%s) failed: %s", MAP_NVS_NAMESPACE,
                 esp_err_to_name(err));
        return false;
    }
    map_kv_t kv = {&s_handle, kv_get, kv_set};
    if (!map_core_init(&s_core, &kv, esp_timer_get_time, MAP_USER_ID)) {
        ESP_LOGE(TAG, "could not initialise the MAP core");
        nvs_close(s_handle);
        return false;
    }
    s_open = true;
    ESP_LOGI(TAG, "ready: %u task(s), mood %s, xp %d",
             (unsigned)map_store_count(&s_core.store),
             map_mood_name(map_pet_mood(&s_core.pet, esp_timer_get_time() / 1000)),
             s_core.pet.xp);
    return true;
}

bool map_handles_command(const char *command) {
    return s_open && map_core_handles(command);
}

cJSON *map_handle_command(const char *command, const cJSON *params) {
    if (!s_open) {
        cJSON *result = cJSON_CreateObject();
        if (result) {
            cJSON_AddBoolToObject(result, "ok", false);
            cJSON *error = cJSON_CreateObject();
            if (error) {
                cJSON_AddStringToObject(error, "code", "unavailable");
                cJSON_AddStringToObject(error, "message",
                                        "MAP storage did not start");
                cJSON_AddItemToObject(result, "error", error);
            }
        }
        return result;
    }
    return map_dispatch(&s_core, command, params);
}

const char *const *map_command_names(void) {
    return map_core_command_names();
}

const char *map_command_description(const char *command) {
    return map_core_command_description(command);
}
