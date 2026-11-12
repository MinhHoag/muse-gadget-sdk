/*
 * MAP pet state.
 *
 * A tiny, integer-only Tamagotchi model (no floats: tenths are plenty and
 * integer decay is exact and testable).
 *
 * Stats move by themselves while MAP sleeps: apply elapsed time, then the
 * current mood falls out of the numbers. Completing a task feeds MAP.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stat ranges are tenths of a percent: 0..1000 == 0.0..100.0. */
#define MAP_PET_FULL 1000

/* Decay rates, per hour, in tenths: 1000 == the whole bar in an hour. */
#define MAP_HUNGER_PER_HOUR    100   /* 10 %/h: a full bar lasts 10 h */
#define MAP_HAPPINESS_PER_HOUR  40   /*  4 %/h: a slow lonely drift */
#define MAP_ENERGY_PER_HOUR     50

typedef enum {
    MAP_MOOD_IDLE = 0,
    MAP_MOOD_HAPPY,
    MAP_MOOD_HUNGRY,
    MAP_MOOD_SAD,
} map_mood_t;

typedef struct {
    int hunger;      /* 0 starved .. MAP_PET_FULL stuffed */
    int happiness;   /* 0 sad .. MAP_PET_FULL delighted */
    int energy;      /* 0 exhausted .. MAP_PET_FULL wide awake */
    int xp;          /* total tasks completed, monotonic */
    int64_t last_ms;       /* when the stats were last applied */
    int64_t happy_until_ms;/* a recent good thing: overrides the stats */
    int64_t born_ms;
} map_pet_t;

/* Start a pet: full-ish, neutral, and "born" now. */
void map_pet_init(map_pet_t *pet, int64_t now_ms);

/* Move stats forward to now_ms. Safe to call as often as you like; decay is
 * computed from elapsed time, not from call count. */
void map_pet_tick(map_pet_t *pet, int64_t now_ms);

/* A task was completed: the main way to feed MAP. */
void map_pet_task_completed(map_pet_t *pet, int64_t now_ms);

/* A task was added: a small nudge, much weaker than completing one. */
void map_pet_task_added(map_pet_t *pet, int64_t now_ms);

/* Direct interaction. Returns false when the action does nothing (e.g. fed
 * while already full). */
bool map_pet_feed(map_pet_t *pet, int64_t now_ms);
bool map_pet_play(map_pet_t *pet, int64_t now_ms);
bool map_pet_pet(map_pet_t *pet, int64_t now_ms);

map_mood_t map_pet_mood(const map_pet_t *pet, int64_t now_ms);
const char *map_mood_name(map_mood_t mood);

#ifdef __cplusplus
}
#endif
