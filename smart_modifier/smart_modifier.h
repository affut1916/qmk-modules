// Copyright 2026 affut1916
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Smart Modifier -- a smarter One Shot Modifier.
//
// A plain OSM(mod) key keeps its mod active until the key you apply it to is
// released. This module makes it stick around a little longer: after the key
// that consumed the mod is released, the mod lingers for `timeout` ms, so a
// fast burst of keys (e.g. Shift + several letters) all get the modifier even
// though you only tapped the OSM once.
//
// Only the OSM keys you opt in through smart_modifier_get() are affected. Every
// other OSM key keeps native behaviour.
//
// Behaviour of an opted-in OSM(mod) key:
//
//   tap        arm the mod; it is applied to the next ordinary key and stays
//              active for `timeout` ms after that key (and every further key)
//              is released. The timeout is a rolling window: each press and
//              release restarts it, and it is suspended while any key is held.
//   double tap lock the mod (SMART_MODIFIER_LOCK, on by default). A locked mod
//              ignores the timeout until the trigger is tapped again. The
//              double-tap window is the usual quick tap term.
//   tap again  cancel the mod (when it is not a double tap).
//   hold       native OSM: a real mod while held, released when let go.
//
// Mods from several triggers combine (tap OSM(LSFT), then OSM(LCTL): the next
// key gets Shift+Ctrl). Modifier and layer keys do not consume the mod, so you
// can hold a real modifier and still use the armed one.
//
// The tap/hold decision is left entirely to QMK core: triggers are ordinary
// OSM(mod) keys and this module only rewrites the tap half, so every tap-hold
// feature (PERMISSIVE_HOLD, HOLD_ON_OTHER_KEY_PRESS, QUICK_TAP_TERM,
// get_tapping_term(), ...) works natively.
//
// Declare your triggers from keymap.c:
//
//   #include "smart_modifier.h"
//
//   smart_modifier_mode_t smart_modifier_get(uint16_t keycode,
//                                            smart_modifier_config_t *cfg) {
//     switch (keycode) {   // `cfg` is pre-filled; only change what you need.
//       case OSM(MOD_LSFT): return SMART_MODIFIER_OSM;
//       case OSM(MOD_LCTL): cfg->timeout = 500; return SMART_MODIFIER_OSM;
//     }
//     return SMART_MODIFIER_NONE;
//   }
//
// Requires QMK_KEYBOARD_H (which pulls in community_modules.h) to be included
// before this header.

#pragma once

#include <stdint.h>
#include <stdbool.h>

// How long the mod lingers after the key that consumed it is released, in
// milliseconds. 0 disables the lingering timeout (the mod stays until it is
// cancelled or another trigger replaces it).
#ifndef SMART_MODIFIER_TIMEOUT
#    define SMART_MODIFIER_TIMEOUT 250
#endif

// How long an armed mod waits for its first ordinary key. 0 means it waits
// forever (the same default as native OSM without ONESHOT_TIMEOUT).
#ifndef SMART_MODIFIER_QUEUE_TIMEOUT
#    define SMART_MODIFIER_QUEUE_TIMEOUT 0
#endif

// Whether a double tap of a trigger locks the mod. A locked mod ignores the
// timeout until the trigger is tapped again.
#ifndef SMART_MODIFIER_LOCK
#    define SMART_MODIFIER_LOCK 1
#endif

typedef enum {
    SMART_MODIFIER_NONE = 0,
    SMART_MODIFIER_OSM, // a plain OSM(mod) key with the lingering behaviour
} smart_modifier_mode_t;

typedef struct {
    uint16_t timeout;       // ms to linger after the consumed key is released; 0 = never
    uint16_t queue_timeout; // ms to wait for the first ordinary key; 0 = never
} smart_modifier_config_t;

// Override in keymap.c. `cfg` is pre-filled with the defaults above before the
// call, so only change what you need. Return SMART_MODIFIER_NONE for OSM keys
// that should behave natively.
smart_modifier_mode_t smart_modifier_get(uint16_t keycode, smart_modifier_config_t *cfg);
