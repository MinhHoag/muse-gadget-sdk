/*
 * MAP pet state implementation. Integer-only, portable C11.
 */
#include "map_pet.h"

#include <string.h>

#define MS_PER_HOUR 3600000LL

static int clamp_stat(int v) {
    if (v < 0) return 0;
    if (v > MAP_PET_FULL) return MAP_PET_FULL;
    return v;
}

static int decay(int value, int per_hour, int64_t elapsed_ms) {
    if (elapsed_ms <= 0 || value <= 0) return value;
    /* Integer per-hour decay, floored so frequent ticks never over-decay. */
    int64_t lost = (int64_t)per_hour * elapsed_ms / MS_PER_HOUR;
    return clamp_stat(value - (int)lost);
}

void map_pet_init(map_pet_t *pet, int64_t now_ms) {
    memset(pet, 0, sizeof(*pet));
    pet->hunger = 800;      /* 80 %: content, not stuffed */
    pet->happiness = 800;
    pet->energy = 900;
    pet->xp = 0;
    pet->born_ms = now_ms;
    pet->last_ms = now_ms;
    pet->happy_until_ms = 0;
}

void map_pet_tick(map_pet_t *pet, int64_t now_ms) {
    int64_t elapsed = now_ms - pet->last_ms;
    if (elapsed <= 0) return;
    pet->hunger = decay(pet->hunger, MAP_HUNGER_PER_HOUR, elapsed);
    pet->happiness = decay(pet->happiness, MAP_HAPPINESS_PER_HOUR, elapsed);
    pet->energy = decay(pet->energy, MAP_ENERGY_PER_HOUR, elapsed);
    pet->last_ms = now_ms;
}

void map_pet_task_completed(map_pet_t *pet, int64_t now_ms) {
    map_pet_tick(pet, now_ms);
    /* This is the meal: a completed task is worth a lot more than a tap. */
    pet->hunger = clamp_stat(pet->hunger + 180);
    pet->happiness = clamp_stat(pet->happiness + 260);
    pet->energy = clamp_stat(pet->energy + 60);
    pet->xp += 1;
    pet->happy_until_ms = now_ms + 45000;   /* visibly pleased for a while */
}

void map_pet_task_added(map_pet_t *pet, int64_t now_ms) {
    map_pet_tick(pet, now_ms);
    pet->happiness = clamp_stat(pet->happiness + 40);
    if (pet->hunger > 0) pet->hunger = clamp_stat(pet->hunger - 5);
}

bool map_pet_feed(map_pet_t *pet, int64_t now_ms) {
    map_pet_tick(pet, now_ms);
    if (pet->hunger >= MAP_PET_FULL) return false;
    pet->hunger = clamp_stat(pet->hunger + 250);
    pet->happy_until_ms = now_ms + 20000;
    return true;
}

bool map_pet_play(map_pet_t *pet, int64_t now_ms) {
    map_pet_tick(pet, now_ms);
    if (pet->energy < 150) return false;   /* too tired to play */
    pet->energy = clamp_stat(pet->energy - 120);
    pet->hunger = clamp_stat(pet->hunger - 60);   /* playing makes it hungry */
    pet->happiness = clamp_stat(pet->happiness + 200);
    pet->happy_until_ms = now_ms + 20000;
    return true;
}

bool map_pet_pet(map_pet_t *pet, int64_t now_ms) {
    map_pet_tick(pet, now_ms);
    pet->happiness = clamp_stat(pet->happiness + 60);
    pet->happy_until_ms = now_ms + 15000;
    return true;
}

map_mood_t map_pet_mood(const map_pet_t *pet, int64_t now_ms) {
    if (now_ms < pet->happy_until_ms) return MAP_MOOD_HAPPY;
    if (pet->hunger < 250 || pet->happiness < 200) return MAP_MOOD_SAD;
    if (pet->hunger < 450 || pet->happiness < 400) return MAP_MOOD_HUNGRY;
    return MAP_MOOD_IDLE;
}

const char *map_mood_name(map_mood_t mood) {
    switch (mood) {
        case MAP_MOOD_HAPPY: return "happy";
        case MAP_MOOD_HUNGRY: return "hungry";
        case MAP_MOOD_SAD: return "sad";
        default: return "idle";
    }
}
