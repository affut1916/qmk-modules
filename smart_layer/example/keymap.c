// Copyright 2026 qmk-modules
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Minimal example showing how to use the Smart Layer module. This is a
// template: adapt the LAYOUT(...) call and matrix size to your board.
//
// Layers:
//   0  BASE
//   1  NAV
//   2  NUM
//
// Slot 0 (SL(0)) : tap  = smart layer 2 (Num), double tap = lock, hold = MO(2)
// Slot 1 (SLT(1)): tap  = KC_TAB,             hold = smart layer 1 (Nav)
// A plain LT(1, KC_SPC) is included to show native LT coexistence.

#include QMK_KEYBOARD_H

#include "smart_layer.h"

enum layers { BASE, NAV, NUM };

const smart_layer_config_t smart_layer_configs[SMART_LAYER_SLOT_COUNT] = {
    [0] = {.layer = NUM, .tap = KC_NO, .timeout = SMART_LAYER_TIMEOUT_DEFAULT, .lock_enable = SMART_LAYER_USE_DEFAULT, .swallow_exit = SMART_LAYER_USE_DEFAULT},
    [1] = {.layer = NAV, .tap = KC_TAB, .timeout = SMART_LAYER_TIMEOUT_DEFAULT, .lock_enable = SMART_LAYER_USE_DEFAULT, .swallow_exit = SMART_LAYER_USE_DEFAULT},
    // Remaining slots fall back to "layer = slot, no tap, default timeout".
};

// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [BASE] = LAYOUT(
        KC_Q,    KC_W,   KC_E,   KC_R,   KC_T,
        KC_A,    KC_S,   KC_D,   KC_F,   KC_G,
        KC_Z,    KC_X,   KC_C,   KC_V,   KC_B,
        SL(0),   SLT(1), LT(NAV, KC_SPC), SL_OFF
    ),
    [NAV] = LAYOUT(
        _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______,
        _______, _______, _______, _______
    ),
    [NUM] = LAYOUT(
        _______, _______, _______, _______, _______,
        KC_1,    KC_2,    KC_3,    _______, _______,
        KC_4,    KC_5,    KC_6,    _______, _______,
        _______, _______, _______, _______
    ),
};
// clang-format on
