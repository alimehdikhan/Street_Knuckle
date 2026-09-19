// =============================================================================
//  scene.h - draws the current game state.
// =============================================================================
#pragma once

#include "game.h"
#include "render.h"

// Renders one whole frame: backdrop, stage, fighters, effects, post and HUD.
// `t` is wall-clock seconds, used for idle animation and torch flicker.
void SceneDrawFrame(float t, bool postFx);
