// Copyright 2026 qmk-modules
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Smart Layer -- thin community module.
//
// The tap/hold decision is left entirely to QMK core: you declare triggers as
// ordinary Layer-Tap keys and the module only rewrites the half it cares about.
// That means every tap-hold feature works natively and with zero configuration
// (PERMISSIVE_HOLD, HOLD_ON_OTHER_KEY_PRESS, CHORDAL_HOLD, FLOW_TAP_TERM,
// SPECULATIVE_HOLD, get_tapping_term(), QUICK_TAP_TERM, ...).
//
// Two trigger flavours, both plain LT keys:
//
//   SL  = LT(layer, KC_NO)   tap  = activate a sticky layer (toggle/lock)
//                            hold = native MO(layer)
//
//   SLT = LT(layer, kc)      tap  = native kc (including native quick-tap)
//                            hold = activate a sticky layer
//
// A sticky layer stays on until one of:
//   * a key press that is not "part of" the layer (see the whitelist below),
//   * the idle timeout (default SMART_LAYER_DEFAULT_TIMEOUT) elapses, or
//   * SL_OFF is pressed.
//
// Smart layers are independent: activating one never turns another off. Each is
// closed on its own by ordinary key presses (per the whitelist), its timeout,
// or SL_OFF. The layer-activation halves of the triggers (SL tap/hold, SLT
// hold) do not count as ordinary presses. An SLT tap is an ordinary key (it
// really sends `kc`) and therefore does participate in the whitelist.
//
// Default whitelist: a key keeps the layer on when the target layer defines a
// non-transparent keycode at that position. Add extra keycodes with
// smart_layer_config_t.continue_list.
//
// Triggers are tracked by keycode, so the same LT may be placed on a physical
// key and/or emitted by a combo, and either one toggles the same sticky layer.
// A combo output is a COMBO_EVENT with no matrix position; it still toggles the
// layer, but it is not an ordinary press and never takes part in auto-exit.
//
// IMPORTANT: leave each trigger's own position transparent on the layer it
// activates, so the trigger keeps resolving to the same LT key and can be
// toggled off / re-held. This also applies to the keys making up a combo that
// outputs a trigger: if they are not transparent on the target layer, the combo
// stops matching once the layer is on.
//
// Declare your triggers from keymap.c:
//
//   #define SL_NUM  LT(NUM, KC_NO)
//   #define SLT_TAB LT(NAV, KC_TAB)
//
//   smart_layer_mode_t smart_layer_get(uint16_t keycode,
//                                      smart_layer_config_t *cfg) {
//     switch (keycode) {   // `cfg` is pre-filled; only change what you need.
//       case SL_NUM:  return SMART_LAYER_SL;
//       case SLT_TAB: cfg->timeout = 500; return SMART_LAYER_SLT;
//     }
//     return SMART_LAYER_NONE;
//   }
//
// Requires QMK_KEYBOARD_H (which pulls in community_modules.h) to be included
// before this header so SMART_LAYER_OFF is in scope.

#pragma once

#include <stdint.h>
#include <stdbool.h>

// Maximum number of sticky layers tracked at the same time. This is unrelated
// to how many layers exist (that is MAX_LAYER).
#ifndef SMART_LAYER_MAX_ACTIVE
#    define SMART_LAYER_MAX_ACTIVE 8
#endif

// Default idle timeout for a sticky layer, in milliseconds.
#ifndef SMART_LAYER_DEFAULT_TIMEOUT
#    define SMART_LAYER_DEFAULT_TIMEOUT 3000
#endif

// Default for whether the key that triggers an automatic exit is swallowed
// (not sent to the host).
#ifndef SMART_LAYER_SWALLOW_EXIT
#    define SMART_LAYER_SWALLOW_EXIT 0
#endif

// Whether a double tap of an SL trigger locks the sticky layer. A locked layer
// ignores both the idle timeout and the whitelist until it is tapped again.
#ifndef SMART_LAYER_SL_DOUBLE_TAP_LOCK
#    define SMART_LAYER_SL_DOUBLE_TAP_LOCK 1
#endif

typedef enum {
    SMART_LAYER_NONE = 0,
    SMART_LAYER_SL,  // tap activates a sticky layer, hold is native MO
    SMART_LAYER_SLT, // tap is native kc, hold activates a sticky layer
} smart_layer_mode_t;

typedef struct {
    uint16_t        timeout;            // ms; 0 disables the timeout
    bool            swallow_exit;       // swallow the key that exits the layer
    const uint16_t *continue_list;      // extra keycodes that keep the layer on
    uint8_t         continue_list_size;
} smart_layer_config_t;

// Override in keymap.c. `cfg` is pre-filled with the defaults above before the
// call, so only change what you need. Return SMART_LAYER_NONE for non-triggers.
smart_layer_mode_t smart_layer_get(uint16_t keycode, smart_layer_config_t *cfg);

// Clears every sticky layer (also turns Caps Word off, if enabled).
#define SL_OFF SMART_LAYER_OFF
