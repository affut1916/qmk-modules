// Copyright 2026 qmk-modules
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Smart Layer for QMK -- a port of urob's zmk-auto-layer / smart layers.
//
// Two distinct keys share one per-slot configuration table:
//
//   SL(slot)   tap  = activate the smart layer (sticky, auto-off)
//              dbl  = lock the layer (disable timeout + whitelist exit; tap again to unlock)
//              hold = MO(config.layer)
//
//   SLT(slot)  tap  = config.tap
//              dbl  = LT-style quick tap (tap, then press-and-hold within the quick-tap
//                     window repeats config.tap; no third/lock state)
//              hold = activate the smart layer (sticky, auto-off)
//
// Plain LT/MO/TG/TT keep their native QMK behaviour and can be used on the same layer.
//
// Configure the slots in your keymap:
//
//   const smart_layer_config_t smart_layer_configs[SMART_LAYER_SLOT_COUNT] = {
//       [0] = { .layer = 3, .tap = KC_TAB },                 // SLT(0)/SL(0) -> layer 3
//       [1] = { .layer = 4, .tap = KC_NO, .timeout = 0 },    // SL(1): no timeout
//   };
//
// Toggle the HRM participation of each group in your config.h, e.g.
//   #define SMART_LAYER_SL_CHORDAL_HOLD 1
//   #define SMART_LAYER_SLT_FLOW_TAP 0

#pragma once

#include <stdint.h>
#include <stdbool.h>

// Number of independent slots. Matches the number of SLT_n / SL_n keycodes.
#ifndef SMART_LAYER_SLOT_COUNT
#    define SMART_LAYER_SLOT_COUNT 16
#endif

// Slot timeout in milliseconds after the last key activity. 0 disables the timeout.
#ifndef SMART_LAYER_DEFAULT_TIMEOUT
#    define SMART_LAYER_DEFAULT_TIMEOUT 300
#endif

// Sentinel meaning "use SMART_LAYER_DEFAULT_TIMEOUT".
#define SMART_LAYER_TIMEOUT_DEFAULT 0xFFFF
// Sentinel for the per-slot tri-state overrides.
#define SMART_LAYER_USE_DEFAULT (-1)

// ---- Per-group HRM participation switches ----------------------------------
// Defaults follow the corresponding QMK feature, but each can be forced on/off
// from config.h. The `#ifndef` guards let a config.h definition win, because
// config.h is processed before this header is included.

#ifndef SMART_LAYER_SL_PERMISSIVE_HOLD
#    if defined(PERMISSIVE_HOLD) || defined(PERMISSIVE_HOLD_PER_KEY)
#        define SMART_LAYER_SL_PERMISSIVE_HOLD 1
#    else
#        define SMART_LAYER_SL_PERMISSIVE_HOLD 0
#    endif
#endif

#ifndef SMART_LAYER_SL_HOLD_ON_OTHER_KEY_PRESS
#    if defined(HOLD_ON_OTHER_KEY_PRESS) || defined(HOLD_ON_OTHER_KEY_PRESS_PER_KEY)
#        define SMART_LAYER_SL_HOLD_ON_OTHER_KEY_PRESS 1
#    else
#        define SMART_LAYER_SL_HOLD_ON_OTHER_KEY_PRESS 0
#    endif
#endif

// SL's tap is a layer action, not a character, so chordal hold (positional
// same-hand detection) is off by default: it would only add latency.
#ifndef SMART_LAYER_SL_CHORDAL_HOLD
#    define SMART_LAYER_SL_CHORDAL_HOLD 0
#endif

#ifndef SMART_LAYER_SL_FLOW_TAP
#    ifdef FLOW_TAP_TERM
#        define SMART_LAYER_SL_FLOW_TAP 1
#    else
#        define SMART_LAYER_SL_FLOW_TAP 0
#    endif
#endif

#ifndef SMART_LAYER_SL_RETRO_TAPPING
#    ifdef RETRO_TAPPING
#        define SMART_LAYER_SL_RETRO_TAPPING 1
#    else
#        define SMART_LAYER_SL_RETRO_TAPPING 0
#    endif
#endif

#ifndef SMART_LAYER_SL_QUICK_TAP
#    define SMART_LAYER_SL_QUICK_TAP 1
#endif

// SL double-tap locks the layer.
#ifndef SMART_LAYER_SL_DOUBLE_TAP_LOCK
#    define SMART_LAYER_SL_DOUBLE_TAP_LOCK 1
#endif

// Whether the key that triggers an automatic exit is swallowed (not sent).
#ifndef SMART_LAYER_SL_SWALLOW_EXIT
#    define SMART_LAYER_SL_SWALLOW_EXIT 0
#endif

#ifndef SMART_LAYER_SLT_PERMISSIVE_HOLD
#    if defined(PERMISSIVE_HOLD) || defined(PERMISSIVE_HOLD_PER_KEY)
#        define SMART_LAYER_SLT_PERMISSIVE_HOLD 1
#    else
#        define SMART_LAYER_SLT_PERMISSIVE_HOLD 0
#    endif
#endif

#ifndef SMART_LAYER_SLT_HOLD_ON_OTHER_KEY_PRESS
#    if defined(HOLD_ON_OTHER_KEY_PRESS) || defined(HOLD_ON_OTHER_KEY_PRESS_PER_KEY)
#        define SMART_LAYER_SLT_HOLD_ON_OTHER_KEY_PRESS 1
#    else
#        define SMART_LAYER_SLT_HOLD_ON_OTHER_KEY_PRESS 0
#    endif
#endif

// SLT's tap is a character, so chordal hold follows the global setting.
#ifndef SMART_LAYER_SLT_CHORDAL_HOLD
#    ifdef CHORDAL_HOLD
#        define SMART_LAYER_SLT_CHORDAL_HOLD 1
#    else
#        define SMART_LAYER_SLT_CHORDAL_HOLD 0
#    endif
#endif

#ifndef SMART_LAYER_SLT_FLOW_TAP
#    ifdef FLOW_TAP_TERM
#        define SMART_LAYER_SLT_FLOW_TAP 1
#    else
#        define SMART_LAYER_SLT_FLOW_TAP 0
#    endif
#endif

#ifndef SMART_LAYER_SLT_RETRO_TAPPING
// Defaults off: retro tapping would turn the sticky layer off again when the
// trigger is held and released on its own, defeating SLT's hold action. Set to
// 1 to opt in.
#    define SMART_LAYER_SLT_RETRO_TAPPING 0
#endif

#ifndef SMART_LAYER_SLT_QUICK_TAP
#    define SMART_LAYER_SLT_QUICK_TAP 1
#endif

// Whether a double tap of SLT repeats the tap key (LT-style auto-repeat).
// (SMART_LAYER_SLT_QUICK_TAP above is the same switch; kept for symmetry.)

#ifndef SMART_LAYER_SLT_SWALLOW_EXIT
#    define SMART_LAYER_SLT_SWALLOW_EXIT 0
#endif

// ---- Keycodes --------------------------------------------------------------

// Requires QMK_KEYBOARD_H (which pulls in community_modules.h) to be included
// first, so the SMART_LAYER_* enum constants are in scope.
#define SLT(slot) ((uint16_t)(SMART_LAYER_SLT_0 + (slot)))
#define SL(slot) ((uint16_t)(SMART_LAYER_SL_0 + (slot)))
#define SL_OFF SMART_LAYER_OFF

// ---- Configuration ---------------------------------------------------------

typedef struct {
    uint8_t  layer;            // target layer (must be < MAX_LAYER)
    uint16_t tap;              // SLT tap keycode; ignored by SL
    uint16_t timeout;          // ms; 0 = disabled; SMART_LAYER_TIMEOUT_DEFAULT = use default
    int8_t   lock_enable;      // -1 = default; else 0/1 (SL lock on double-tap)
    int8_t   swallow_exit;     // -1 = default; else 0/1 (swallow the exit-triggering key)
    uint8_t  continue_list_size;
    const uint16_t *continue_list; // extra whitelist keycodes
    bool     ignore_alphas;
    bool     ignore_numbers;
    bool     ignore_modifiers;
} smart_layer_config_t;

// Define this in your keymap.c to configure the slots. A weak default table is
// provided, where slot n targets layer n, with no tap key and the default
// timeout. Setting .lock_enable / .swallow_exit to SMART_LAYER_USE_DEFAULT
// defers to the compile-time switches above.
extern const smart_layer_config_t smart_layer_configs[SMART_LAYER_SLOT_COUNT];
