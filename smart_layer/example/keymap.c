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
// Triggers are ordinary LT keys:
//   SL_NUM  = LT(NUM, KC_NO)   tap = sticky Num (double-tap locks), hold = MO(NUM)
//   SLT_TAB = LT(NAV, KC_TAB)  tap = Tab, hold = sticky Nav
// A plain LT(NAV, KC_SPC) coexists and behaves natively.

#include QMK_KEYBOARD_H

#include "smart_layer.h"

enum layers { BASE, NAV, NUM };

#define SL_NUM LT(NUM, KC_NO)
#define SLT_TAB LT(NAV, KC_TAB)

smart_layer_mode_t smart_layer_get(uint16_t keycode, smart_layer_config_t *cfg) {
    // `cfg` arrives pre-filled with the defaults; override as needed.
    switch (keycode) {
        case SL_NUM:
            return SMART_LAYER_SL; // Double-tap lock is controlled by
                                   // SMART_LAYER_SL_DOUBLE_TAP_LOCK (default on).
        case SLT_TAB:
            cfg->timeout = 500; // Override the default 3000 ms.
            return SMART_LAYER_SLT;
    }
    return SMART_LAYER_NONE;
}

// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [BASE] = LAYOUT(
        KC_Q,     KC_W,   KC_E,   KC_R,   KC_T,
        KC_A,     KC_S,   KC_D,   KC_F,   KC_G,
        KC_Z,     KC_X,   KC_C,   KC_V,   KC_B,
        SL_NUM,   SLT_TAB, LT(NAV, KC_SPC), SL_OFF
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

// Optional: the very same SL_NUM can also be emitted by a combo. The module
// identifies triggers by keycode, so the physical key and this combo share one
// sticky layer. The combo members (KC_Q/KC_W) stay transparent on the NUM layer
// (see the keymap above). Requires COMBO_ENABLE in your config.
#ifdef COMBO_ENABLE
const uint16_t PROGMEM combo_num[] = {KC_Q, KC_W, COMBO_END};
combo_t key_combos[] = {
    COMBO(combo_num, SL_NUM),
};
#endif
