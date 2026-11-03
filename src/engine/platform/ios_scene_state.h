#pragma once
#include <stdbool.h>

// UIKit delivers these callbacks on the main thread. External scenes must not
// launch another game or send lifecycle events to the primary game's renderer.
typedef struct PTIosSceneState {
    const void* primary_scene;
    bool main_started;
} PTIosSceneState;

static inline bool PTIosAcceptScene(PTIosSceneState* state, const void* scene, bool application_role) {
    if (!scene || !application_role || (state->primary_scene && state->primary_scene != scene)) return false;
    state->primary_scene = scene;
    return true;
}

static inline bool PTIosIsPrimaryScene(const PTIosSceneState* state, const void* scene) {
    return scene && state->primary_scene == scene;
}

static inline bool PTIosBeginMain(PTIosSceneState* state) {
    if (state->main_started) return false;
    state->main_started = true;
    return true;
}
