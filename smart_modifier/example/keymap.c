// Copyright 2026 affut1916
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Minimal example showing how to use the Smart Modifier module. This is a
// template: adapt the LAYOUT(...) call and matrix size to your board.
//
// Triggers are ordinary modifier and Mod-Tap keycodes; only the ones the
// callback opts in get the lingering behaviour. Every other key stays native.
//
//   KC_LSFT        Shift; lingers 250 ms after release, double tap locks it
//   KC_RCTL        right Ctrl; lingers 500 ms
//   LGUI_T(KC_I)   tap = i, hold = GUI; the GUI lingers 250 ms after the hold
//   KC_LALT        a plain Alt, never declared, so it stays fully native
//   LSFT_T(KC_A)   a plain Mod-Tap, never declared, so it stays fully native

#include QMK_KEYBOARD_H

#include "smart_modifier.h"

smart_modifier_mode_t smart_modifier_get(uint16_t keycode, smart_modifier_config_t *cfg) {
    // `cfg` arrives pre-filled with the defaults; override as needed.
    switch (keycode) {
        case KC_LSFT:
            return SMART_MODIFIER_MOD; // Default 250 ms, double tap locks.

        case KC_RCTL:
            cfg->timeout = 500; // Override the default 250 ms.
            return SMART_MODIFIER_MOD;

        case LGUI_T(KC_I):
            return SMART_MODIFIER_MOD_TAP; // Tap = i; hold = GUI, lingering.
    }
    return SMART_MODIFIER_NONE;
}

// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT(
        KC_Q,     KC_W,           KC_E,   KC_R,   KC_T,
        KC_A,     KC_S,           KC_D,   KC_F,   KC_G,
        KC_LSFT,  KC_RCTL,        LGUI_T(KC_I),    KC_LALT
    ),
};
// clang-format on
