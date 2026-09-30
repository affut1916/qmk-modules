// Copyright 2026 affut1916
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Smart Modifier -- plain modifiers and Mod-Taps that linger.
//
// A plain modifier (KC_LSFT, KC_RCTL, ...) is normally active only while it is
// held. This module keeps it alive for `timeout` ms after it is released (a
// rolling window), so a fast burst of keys all gets the modifier even though
// the mod key was already let go.
//
// A Mod-Tap (LGUI_T(KC_I), ...) behaves natively: tap sends the base key, hold
// is the modifier. This module only keeps the modifier alive for `timeout` ms
// after the hold is released.
//
// Only the keycodes you opt in through smart_modifier_get() are affected. Every
// other key (including other modifiers and Mod-Taps) keeps native behaviour.
//
// The module never decides tap/hold itself: triggers are ordinary modifier and
// Mod-Tap keycodes, so QMK core resolves them and every tap-hold feature
// (PERMISSIVE_HOLD, HOLD_ON_OTHER_KEY_PRESS, QUICK_TAP_TERM, CHORDAL_HOLD,
// get_tapping_term(), ...) works natively.
//
// Behaviour of an opted-in key:
//
//   plain mod  hold      native modifier while held.
//              release   the mod lingers for `timeout` ms; every press and
//                        release restarts the window, and the window is
//                        suspended while any key is held, so a long keypress is
//                        never cut short.
//              double tap lock the mod (SMART_MODIFIER_LOCK, on by default); a
//                        press of the same trigger again unlocks it.
//   mod-tap    tap       native base key (KC_I); no lingering.
//              hold      native modifier while held; the modifier lingers for
//                        `timeout` ms after the hold is released.
//              (Mod-Taps never lock.)
//
// Mods from several triggers combine (a lingering Shift plus a lingering Ctrl
// give the next key Shift+Ctrl). Every trigger has its own timeout and its own
// lock, so one trigger never affects another.
//
// Declare your triggers from keymap.c:
//
//   #include "smart_modifier.h"
//
//   smart_modifier_mode_t smart_modifier_get(uint16_t keycode,
//                                            smart_modifier_config_t *cfg) {
//     switch (keycode) {   // `cfg` is pre-filled; only change what you need.
//       case KC_LSFT:      return SMART_MODIFIER_MOD;
//       case KC_RCTL:      cfg->timeout = 500; return SMART_MODIFIER_MOD;
//       case LGUI_T(KC_I): return SMART_MODIFIER_MOD_TAP;
//     }
//     return SMART_MODIFIER_NONE;
//   }
//
// Requires QMK_KEYBOARD_H (which pulls in community_modules.h) to be included
// before this header.

#pragma once

#include <stdint.h>
#include <stdbool.h>

// How long a mod lingers after its trigger is released, in milliseconds.
// 0 disables the lingering (the mod is active only while the trigger is held);
// the double-tap lock is not affected.
#ifndef SMART_MODIFIER_TIMEOUT
#    define SMART_MODIFIER_TIMEOUT 400
#endif

// Whether a double tap of a plain-modifier trigger locks its mod. A locked mod
// ignores the timeout until the trigger is pressed again. Mod-Taps never lock.
#ifndef SMART_MODIFIER_LOCK
#    define SMART_MODIFIER_LOCK 1
#endif

// Maximum number of triggers whose mods can linger at the same time. This is an
// implementation capacity, not a per-key option; a trigger pressed while every
// slot is taken simply behaves natively.
#ifndef SMART_MODIFIER_MAX_ACTIVE
#    define SMART_MODIFIER_MAX_ACTIVE 4
#endif

typedef enum {
    SMART_MODIFIER_NONE = 0,
    SMART_MODIFIER_MOD,     // a plain modifier keycode (KC_LSFT, KC_RCTL, ...)
    SMART_MODIFIER_MOD_TAP, // a Mod-Tap keycode (LGUI_T(KC_I), ...)
} smart_modifier_mode_t;

typedef struct {
    uint16_t timeout; // ms to linger after the trigger is released; 0 = never
    bool     lock;    // allow the plain-modifier double-tap lock
} smart_modifier_config_t;

// Override in keymap.c. `cfg` is pre-filled with the defaults above before the
// call, so only change what you need. Return SMART_MODIFIER_NONE for keycodes
// that should behave natively.
smart_modifier_mode_t smart_modifier_get(uint16_t keycode, smart_modifier_config_t *cfg);
