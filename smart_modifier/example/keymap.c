// Copyright 2026 affut1916
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Minimal example showing how to use the Smart Modifier module. This is a
// template: adapt the LAYOUT(...) call and matrix size to your board.
//
// Triggers are ordinary OSM keys; only the ones the callback opts in get the
// lingering behaviour. Every other OSM key stays native.
//
//   OSM(MOD_LSFT)  tap = Shift that lingers a moment  (default 250 ms)
//   OSM(MOD_LCTL)  tap = Ctrl that lingers longer       (500 ms)
//   OSM(MOD_LALT)  tap = plain native One Shot Alt

#include QMK_KEYBOARD_H

#include "smart_modifier.h"

smart_modifier_mode_t smart_modifier_get(uint16_t keycode, smart_modifier_config_t *cfg) {
    // `cfg` arrives pre-filled with the defaults; override as needed.
    switch (keycode) {
        case OSM(MOD_LSFT):
            return SMART_MODIFIER_OSM;

        case OSM(MOD_LCTL):
            cfg->timeout       = 500; // Override the default 250 ms.
            cfg->queue_timeout = 1000; // Give up if no key follows within 1 s.
            return SMART_MODIFIER_OSM;
    }
    return SMART_MODIFIER_NONE;
}

// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT(
        KC_Q,           KC_W,   KC_E,   KC_R,   KC_T,
        KC_A,           KC_S,   KC_D,   KC_F,   KC_G,
        OSM(MOD_LSFT),  OSM(MOD_LCTL), OSM(MOD_LALT), KC_Z
    ),
};
// clang-format on
