// Copyright 2026 qmk-modules
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Smart Layer engine. See smart_layer.h for the user-facing API.
//
// The engine fully owns the SL/SLT keycodes: it consumes them in
// pre_process_record so the QMK tap-hold state machine never sees them, and it
// re-implements the subset of tap-hold behaviour that applies to each group
// (SL vs SLT), driven by the SMART_LAYER_* switches.

#include QMK_KEYBOARD_H

#include "smart_layer.h"

#ifdef CAPS_WORD_ENABLE
#    include "process_caps_word.h"
#    include "caps_word.h"
#endif

ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 1, 0);

// The module declares 16 SLT and 16 SL keycodes; SMART_LAYER_SLOT_COUNT may be
// lowered, in which case the unused high keycodes must not be mapped.
STATIC_ASSERT(SMART_LAYER_SLOT_COUNT <= 16, "Smart Layer declares only 16 slots");

// Which key activated the slot.
enum { SLM_VIA_SL = 0, SLM_VIA_SLT = 1 };

// Slot lifecycle mode.
enum {
    SLM_IDLE = 0,   // inactive
    SLM_UNDECIDED,  // trigger pressed, tap/hold not settled
    SLM_STICKY,     // smart layer activated and sticky (SL tap / SLT hold)
    SLM_MO,         // momentary layer held (SL hold)
    SLM_LOCKED,     // SL double-tap lock
    SLM_QUICKTAP,   // SLT quick-tap: tap key currently registered
};

typedef struct {
    uint8_t     mode;
    bool        key_held;    // the SL/SLT trigger key is physically down
    bool        interrupted; // another key was used while this slot was held
    uint8_t     via;
    uint16_t    press_time;
    uint16_t    tap_release_time;
    uint16_t    last_activity;
    keyrecord_t press_record;
} smart_slot_t;

static smart_slot_t slots[SMART_LAYER_SLOT_COUNT];

// Number of physical keys currently held (for the sticky timeout).
static uint16_t held_key_count = 0;

#ifdef FLOW_TAP_TERM
// Previous non-slot key, for flow tap (time + key predicate).
static uint16_t last_key_time = 0;
static uint16_t last_keycode  = KC_NO;
#endif

// A key being swallowed because it triggered an automatic exit.
static bool     swallow_active = false;
static keypos_t swallowed_key  = {0};

// Weak default configuration: slot n targets layer n, no tap key, no timeout.
#define SMART_LAYER_DEFAULT_ENTRY(n)                                                                                        \
    [n] = {.layer = (n), .tap = KC_NO, .timeout = SMART_LAYER_TIMEOUT_DEFAULT, .lock_enable = SMART_LAYER_USE_DEFAULT,        \
           .swallow_exit = SMART_LAYER_USE_DEFAULT, .continue_list = NULL, .continue_list_size = 0}

__attribute__((weak)) const smart_layer_config_t smart_layer_configs[SMART_LAYER_SLOT_COUNT] = {
    SMART_LAYER_DEFAULT_ENTRY(0),  SMART_LAYER_DEFAULT_ENTRY(1),  SMART_LAYER_DEFAULT_ENTRY(2),  SMART_LAYER_DEFAULT_ENTRY(3),
    SMART_LAYER_DEFAULT_ENTRY(4),  SMART_LAYER_DEFAULT_ENTRY(5),  SMART_LAYER_DEFAULT_ENTRY(6),  SMART_LAYER_DEFAULT_ENTRY(7),
    SMART_LAYER_DEFAULT_ENTRY(8),  SMART_LAYER_DEFAULT_ENTRY(9),  SMART_LAYER_DEFAULT_ENTRY(10), SMART_LAYER_DEFAULT_ENTRY(11),
    SMART_LAYER_DEFAULT_ENTRY(12), SMART_LAYER_DEFAULT_ENTRY(13), SMART_LAYER_DEFAULT_ENTRY(14), SMART_LAYER_DEFAULT_ENTRY(15),
};

// ---- Group / config helpers ------------------------------------------------

static inline bool smart_is_sl_group(uint8_t via) {
    return via == SLM_VIA_SL;
}

static inline uint16_t smart_slot_keycode(uint8_t slot, uint8_t via) {
    return smart_is_sl_group(via) ? (uint16_t)SL(slot) : (uint16_t)SLT(slot);
}

static inline uint16_t smart_tapping_term(uint8_t slot, uint8_t via) {
    return GET_TAPPING_TERM(smart_slot_keycode(slot, via), &slots[slot].press_record);
}

static inline uint16_t smart_quick_tap_term(uint8_t slot, uint8_t via) {
    return GET_QUICK_TAP_TERM(smart_slot_keycode(slot, via), &slots[slot].press_record);
}

static inline bool smart_permissive_hold(uint8_t slot, uint8_t via) {
#if defined(PERMISSIVE_HOLD_PER_KEY)
    if (smart_is_sl_group(via) ? !SMART_LAYER_SL_PERMISSIVE_HOLD : !SMART_LAYER_SLT_PERMISSIVE_HOLD) {
        return false;
    }
    return get_permissive_hold(smart_slot_keycode(slot, via), &slots[slot].press_record);
#else
    return smart_is_sl_group(via) ? (SMART_LAYER_SL_PERMISSIVE_HOLD != 0) : (SMART_LAYER_SLT_PERMISSIVE_HOLD != 0);
#endif
}

static inline bool smart_hold_on_other_key_press(uint8_t slot, uint8_t via) {
#if defined(HOLD_ON_OTHER_KEY_PRESS_PER_KEY)
    if (smart_is_sl_group(via) ? !SMART_LAYER_SL_HOLD_ON_OTHER_KEY_PRESS : !SMART_LAYER_SLT_HOLD_ON_OTHER_KEY_PRESS) {
        return false;
    }
    return get_hold_on_other_key_press(smart_slot_keycode(slot, via), &slots[slot].press_record);
#else
    return smart_is_sl_group(via) ? (SMART_LAYER_SL_HOLD_ON_OTHER_KEY_PRESS != 0) : (SMART_LAYER_SLT_HOLD_ON_OTHER_KEY_PRESS != 0);
#endif
}

static inline bool smart_flow_tap_enabled(uint8_t via) {
    return smart_is_sl_group(via) ? (SMART_LAYER_SL_FLOW_TAP != 0) : (SMART_LAYER_SLT_FLOW_TAP != 0);
}

static inline bool smart_retro_tapping_enabled(uint8_t via) {
    return smart_is_sl_group(via) ? (SMART_LAYER_SL_RETRO_TAPPING != 0) : (SMART_LAYER_SLT_RETRO_TAPPING != 0);
}

#ifdef FLOW_TAP_TERM
static inline bool smart_quick_tap_enabled(uint8_t via) {
    return smart_is_sl_group(via) ? (SMART_LAYER_SL_QUICK_TAP != 0) : (SMART_LAYER_SLT_QUICK_TAP != 0);
}
#endif

#ifdef CHORDAL_HOLD
static inline bool smart_chordal_enabled(uint8_t via) {
    return smart_is_sl_group(via) ? (SMART_LAYER_SL_CHORDAL_HOLD != 0) : (SMART_LAYER_SLT_CHORDAL_HOLD != 0);
}
#endif

#ifdef FLOW_TAP_TERM
// Flow Tap acts when the trigger is pressed shortly after a "flow tap" key and
// the trigger's own tap target is also a flow-tap key. For SL (whose tap is a
// layer action) the group switch alone is enough.
static bool smart_flow_tap_eligible(uint8_t slot, uint8_t via) {
    if (!smart_flow_tap_enabled(via)) {
        return false;
    }
    if (!is_flow_tap_key(last_keycode)) {
        return false;
    }
    if (smart_is_sl_group(via)) {
        return true;
    }
    uint16_t tap = smart_layer_configs[slot].tap;
    return tap == KC_NO || is_flow_tap_key(tap);
}
#endif

static inline uint16_t smart_slot_timeout(uint8_t slot) {
    uint16_t t = smart_layer_configs[slot].timeout;
    return t == SMART_LAYER_TIMEOUT_DEFAULT ? SMART_LAYER_DEFAULT_TIMEOUT : t;
}

static inline bool smart_lock_enabled(uint8_t slot) {
    int8_t v = smart_layer_configs[slot].lock_enable;
    return v == SMART_LAYER_USE_DEFAULT ? (SMART_LAYER_SL_DOUBLE_TAP_LOCK != 0) : (v != 0);
}

static inline bool smart_swallow_exit(uint8_t slot, uint8_t via) {
    int8_t v = smart_layer_configs[slot].swallow_exit;
    if (v != SMART_LAYER_USE_DEFAULT) {
        return v != 0;
    }
    return smart_is_sl_group(via) ? (SMART_LAYER_SL_SWALLOW_EXIT != 0) : (SMART_LAYER_SLT_SWALLOW_EXIT != 0);
}

static inline bool smart_layer_valid(uint8_t slot) {
    return smart_layer_configs[slot].layer < MAX_LAYER;
}

// ---- Keycode predicates ----------------------------------------------------

static uint16_t smart_strip_mods(uint16_t keycode) {
    if (keycode >= QK_MODS && keycode <= QK_MODS_MAX) {
        return QK_MODS_GET_BASIC_KEYCODE(keycode);
    }
    return keycode;
}

static bool smart_is_alpha(uint16_t keycode) {
    keycode = smart_strip_mods(keycode);
    return keycode >= KC_A && keycode <= KC_Z;
}

static bool smart_is_numeric(uint16_t keycode) {
    keycode = smart_strip_mods(keycode);
    return (keycode >= KC_1 && keycode <= KC_0) || (keycode >= KC_KP_1 && keycode <= KC_KP_0);
}

static bool smart_is_modifier(uint16_t keycode) {
    return keycode >= KC_LEFT_CTRL && keycode <= KC_RIGHT_GUI;
}

static bool smart_continue_list_match(const smart_layer_config_t *cfg, uint16_t keycode) {
    if (cfg->continue_list == NULL) {
        return false;
    }
    for (uint8_t i = 0; i < cfg->continue_list_size; i++) {
        if (cfg->continue_list[i] == keycode) {
            return true;
        }
    }
    return false;
}

// Returns true if a slot other than `exclude` is keeping `layer` on.
static bool smart_other_slot_on_layer(uint8_t layer, uint8_t exclude) {
    for (uint8_t i = 0; i < SMART_LAYER_SLOT_COUNT; i++) {
        if (i == exclude || !smart_layer_valid(i) || smart_layer_configs[i].layer != layer) {
            continue;
        }
        uint8_t m = slots[i].mode;
        if (m == SLM_STICKY || m == SLM_LOCKED || m == SLM_MO) {
            return true;
        }
    }
    return false;
}

static void smart_layer_on(uint8_t slot) {
    if (smart_layer_valid(slot)) {
        layer_on(smart_layer_configs[slot].layer);
    }
}

static void smart_layer_off(uint8_t slot) {
    if (smart_layer_valid(slot) && !smart_other_slot_on_layer(smart_layer_configs[slot].layer, slot)) {
        layer_off(smart_layer_configs[slot].layer);
    }
}

// ---- Settling --------------------------------------------------------------

static void smart_settle_tap(uint8_t slot) {
    const smart_layer_config_t *cfg = &smart_layer_configs[slot];
    if (smart_is_sl_group(slots[slot].via)) {
        // SL tap: activate the smart layer (sticky).
        slots[slot].mode = SLM_STICKY;
        smart_layer_on(slot);
    } else {
        // SLT tap: emit the tap keycode, no layer.
        slots[slot].mode = SLM_IDLE;
        if (cfg->tap != KC_NO) {
            tap_code16(cfg->tap);
        }
    }
    slots[slot].tap_release_time = timer_read();
    slots[slot].last_activity    = timer_read();
}

static void smart_settle_hold(uint8_t slot) {
    if (smart_is_sl_group(slots[slot].via)) {
        slots[slot].mode = SLM_MO; // SL hold: momentary layer.
    } else {
        slots[slot].mode = SLM_STICKY; // SLT hold: sticky smart layer.
    }
    smart_layer_on(slot);
    slots[slot].last_activity = timer_read();
}

static void smart_settle_off(uint8_t slot) {
    if (slots[slot].mode == SLM_QUICKTAP) {
        const smart_layer_config_t *cfg = &smart_layer_configs[slot];
        if (cfg->tap != KC_NO) {
            unregister_code16(cfg->tap);
        }
    }
    slots[slot].mode = SLM_IDLE;
    smart_layer_off(slot);
}

// ---- Exit on non-whitelist key --------------------------------------------

static bool smart_should_continue(uint8_t slot, uint16_t keycode, keyrecord_t *record) {
    const smart_layer_config_t *cfg = &smart_layer_configs[slot];

    if (cfg->ignore_alphas && smart_is_alpha(keycode)) {
        return true;
    }
    if (cfg->ignore_numbers && smart_is_numeric(keycode)) {
        return true;
    }
    if (cfg->ignore_modifiers && smart_is_modifier(keycode)) {
        return true;
    }
    if (smart_continue_list_match(cfg, keycode)) {
        return true;
    }

    // Default whitelist: keys defined on the smart layer itself.
    if (IS_KEYEVENT(record->event) && smart_layer_valid(slot)) {
        uint16_t on_layer = keymap_key_to_keycode(cfg->layer, record->event.key);
        if (on_layer != KC_TRANSPARENT && on_layer != KC_NO) {
            return true;
        }
    }

    // Non-key events (combos, macros) only continue if whitelisted above.
    return false;
}

// ---- Trigger key handling --------------------------------------------------

static bool smart_handle_slot_key(uint8_t slot, bool is_slt, bool pressed, keyrecord_t *record) {
    uint8_t  via  = is_slt ? SLM_VIA_SLT : SLM_VIA_SL;
    uint16_t now  = timer_read();
    uint8_t  mode = slots[slot].mode;

    if (pressed) {
        slots[slot].press_record = *record;
        slots[slot].key_held     = true;
        slots[slot].interrupted  = false;
        slots[slot].via          = via;

        // Flow tap: the trigger pressed shortly after another key settles as a
        // tap immediately, removing input latency during fast typing. Skipped
        // when this press is itself a quick-tap / toggle, which take priority.
#ifdef FLOW_TAP_TERM
        if (mode == SLM_IDLE && smart_flow_tap_eligible(slot, via) && timer_elapsed(last_key_time) < FLOW_TAP_TERM &&
            !(smart_quick_tap_enabled(via) && timer_elapsed(slots[slot].tap_release_time) < smart_quick_tap_term(slot, via))) {
            smart_settle_tap(slot);
            return true;
        }
#endif

        if (!smart_is_sl_group(via)) {
            // SLT re-pressed while its sticky layer is on: turn the layer off
            // and treat this as a fresh press.
            if (mode == SLM_STICKY) {
                smart_settle_off(slot);
                mode = slots[slot].mode;
            }

            // SLT quick tap: a second press shortly after a tap registers the
            // tap key again (LT-style auto-repeat).
            if ((SMART_LAYER_SLT_QUICK_TAP != 0) && mode == SLM_IDLE &&
                timer_elapsed(slots[slot].tap_release_time) < smart_quick_tap_term(slot, via)) {
                const smart_layer_config_t *cfg = &smart_layer_configs[slot];
                if (cfg->tap != KC_NO) {
                    register_code16(cfg->tap);
                }
                slots[slot].mode          = SLM_QUICKTAP;
                slots[slot].via           = via;
                slots[slot].last_activity = now;
                return true;
            }
            slots[slot].mode       = SLM_UNDECIDED;
            slots[slot].via        = via;
            slots[slot].press_time = now;
            return true;
        }

        // SL
        if (mode == SLM_LOCKED) {
            slots[slot].mode = SLM_IDLE; // tap to unlock
            smart_layer_off(slot);
            return true;
        }
        if (smart_lock_enabled(slot) && mode == SLM_STICKY &&
            timer_elapsed(slots[slot].tap_release_time) < smart_quick_tap_term(slot, via)) {
            slots[slot].mode = SLM_LOCKED; // double tap: lock
            return true;
        }
        if (mode == SLM_STICKY && timer_elapsed(slots[slot].tap_release_time) >= smart_quick_tap_term(slot, via)) {
            slots[slot].mode = SLM_IDLE; // tap outside window toggles off
            smart_layer_off(slot);
            return true;
        }
        slots[slot].mode       = SLM_UNDECIDED;
        slots[slot].via        = via;
        slots[slot].press_time = now;
        return true;
    }

    // Release.
    slots[slot].key_held = false;
    switch (mode) {
        case SLM_UNDECIDED:
            smart_settle_tap(slot);
            break;
        case SLM_MO:
            if (smart_retro_tapping_enabled(via) && !slots[slot].interrupted) {
                // Released after the tapping term with no other key: retro tap.
                if (smart_is_sl_group(via)) {
                    slots[slot].mode = SLM_STICKY; // SL retro tap -> sticky layer.
                } else {
                    const smart_layer_config_t *cfg = &smart_layer_configs[slot];
                    slots[slot].mode                = SLM_IDLE;
                    smart_layer_off(slot);
                    if (cfg->tap != KC_NO) {
                        tap_code16(cfg->tap);
                    }
                }
                slots[slot].tap_release_time = timer_read();
            } else {
                slots[slot].mode = SLM_IDLE;
                smart_layer_off(slot);
            }
            break;
        case SLM_STICKY:
            if (!smart_is_sl_group(via) && smart_retro_tapping_enabled(via) && !slots[slot].interrupted) {
                // SLT held past the term without interruption: send the tap.
                const smart_layer_config_t *cfg = &smart_layer_configs[slot];
                slots[slot].mode = SLM_IDLE;
                smart_layer_off(slot);
                if (cfg->tap != KC_NO) {
                    tap_code16(cfg->tap);
                }
                slots[slot].tap_release_time = timer_read();
            }
            // SL sticky stays on.
            break;
        case SLM_QUICKTAP: {
            const smart_layer_config_t *cfg = &smart_layer_configs[slot];
            if (cfg->tap != KC_NO) {
                unregister_code16(cfg->tap);
            }
            slots[slot].mode             = SLM_IDLE;
            slots[slot].tap_release_time = timer_read();
            break;
        }
        default:
            break;
    }
    return true;
}

// ---- Main event hook -------------------------------------------------------

// Returns true if the event was consumed.
static bool smart_process_event(uint16_t keycode, keyrecord_t *record) {
    const bool     pressed = record->event.pressed;
    const uint16_t now     = timer_read();

    // Swallow the release of a key consumed to trigger an automatic exit.
    // The corresponding press was consumed before held_key_count was updated,
    // so do not touch the counter here.
    if (swallow_active && IS_KEYEVENT(record->event) && !pressed && KEYEQ(record->event.key, swallowed_key)) {
        swallow_active = false;
        return true;
    }

    // Our slot keys.
    if (keycode >= (uint16_t)SLT(0) && keycode < (uint16_t)SLT(0) + SMART_LAYER_SLOT_COUNT) {
        return smart_handle_slot_key((uint8_t)(keycode - (uint16_t)SLT(0)), true, pressed, record);
    }
    if (keycode >= (uint16_t)SL(0) && keycode < (uint16_t)SL(0) + SMART_LAYER_SLOT_COUNT) {
        return smart_handle_slot_key((uint8_t)(keycode - (uint16_t)SL(0)), false, pressed, record);
    }
    if (keycode == SL_OFF) {
        if (pressed) {
            for (uint8_t i = 0; i < SMART_LAYER_SLOT_COUNT; i++) {
                smart_settle_off(i);
            }
#ifdef CAPS_WORD_ENABLE
            caps_word_off();
#endif
        }
        return true;
    }

    if (!IS_KEYEVENT(record->event)) {
        // Non-key events: only close sticky slots whose whitelist names them.
        for (uint8_t i = 0; i < SMART_LAYER_SLOT_COUNT; i++) {
            if (slots[i].mode == SLM_STICKY && !smart_continue_list_match(&smart_layer_configs[i], keycode)) {
                smart_settle_off(i);
            }
        }
        return false;
    }

    if (pressed) {
        held_key_count++;
    } else if (held_key_count > 0) {
        held_key_count--;
    }

    // Snapshot which sticky slots were already active before this event, so the
    // key that triggers an activation is not immediately treated as an exit.
    bool was_sticky[SMART_LAYER_SLOT_COUNT];
    for (uint8_t i = 0; i < SMART_LAYER_SLOT_COUNT; i++) {
        was_sticky[i] = (slots[i].mode == SLM_STICKY || slots[i].mode == SLM_LOCKED);
    }

    // Settle undecided slots now that another key event arrived.
    for (uint8_t i = 0; i < SMART_LAYER_SLOT_COUNT; i++) {
        if (slots[i].mode == SLM_UNDECIDED) {
            const uint8_t via         = slots[i].via;
            bool          settle_hold = false;
            bool          settle_tap  = false;

            // Any other key event disqualifies retro tapping.
            slots[i].interrupted = true;

#ifdef CHORDAL_HOLD
            if (smart_chordal_enabled(via) && pressed &&
                !get_chordal_hold(smart_slot_keycode(i, via), &slots[i].press_record, keycode, record)) {
                settle_tap = true;
            }
#endif
            if (!settle_tap && !settle_hold && pressed && smart_hold_on_other_key_press(i, via)) {
                settle_hold = true;
            }
            if (!settle_tap && !settle_hold && !pressed && smart_permissive_hold(i, via)) {
                settle_hold = true;
            }

            if (settle_tap) {
                smart_settle_tap(i);
            } else if (settle_hold) {
                smart_settle_hold(i);
            }
        } else if (slots[i].key_held) {
            // A non-trigger key was used while the trigger was held.
            slots[i].interrupted = true;
        }
    }

    if (pressed) {
#ifdef FLOW_TAP_TERM
        last_key_time = now;
        last_keycode  = keycode;
#endif

        // Whitelist auto-exit for slots that were already sticky.
        uint8_t exit_slot           = SMART_LAYER_SLOT_COUNT;
        bool    exit_should_swallow = false;

        for (uint8_t i = 0; i < SMART_LAYER_SLOT_COUNT; i++) {
            if (slots[i].mode != SLM_STICKY) {
                continue;
            }
            if (was_sticky[i] && !smart_should_continue(i, keycode, record)) {
                exit_slot           = i;
                exit_should_swallow = smart_swallow_exit(i, slots[i].via);
                smart_settle_off(i);
            } else {
                // A continuing key refreshes the idle timeout.
                slots[i].last_activity = now;
            }
        }

        if (exit_slot < SMART_LAYER_SLOT_COUNT && exit_should_swallow) {
            swallow_active = true;
            swallowed_key  = record->event.key;
            return true;
        }
    }

    return false;
}

bool pre_process_record_smart_layer_user(uint16_t keycode, keyrecord_t *record) {
    return !smart_process_event(keycode, record);
}

// ---- Housekeeping / layer state -------------------------------------------

void housekeeping_task_smart_layer_user(void) {
    // While any smart trigger is physically held, suspend the idle timeout so a
    // long momentary hold is never cut short.
    bool any_trigger_held = false;
    for (uint8_t i = 0; i < SMART_LAYER_SLOT_COUNT; i++) {
        if (slots[i].key_held) {
            any_trigger_held = true;
            break;
        }
    }

    for (uint8_t i = 0; i < SMART_LAYER_SLOT_COUNT; i++) {
        if (slots[i].mode == SLM_UNDECIDED) {
            if (timer_elapsed(slots[i].press_time) >= smart_tapping_term(i, slots[i].via)) {
                smart_settle_hold(i);
            }
            continue;
        }
        if (slots[i].mode == SLM_STICKY) {
            uint16_t to = smart_slot_timeout(i);
            if (to != 0 && !any_trigger_held && !slots[i].key_held && held_key_count == 0 &&
                timer_elapsed(slots[i].last_activity) >= to) {
                smart_settle_off(i);
            }
        }
    }
}

#if !defined(NO_ACTION_LAYER)
layer_state_t layer_state_set_smart_layer_user(layer_state_t state) {
    for (uint8_t i = 0; i < SMART_LAYER_SLOT_COUNT; i++) {
        if ((slots[i].mode == SLM_STICKY || slots[i].mode == SLM_LOCKED) && smart_layer_valid(i)) {
            state |= ((layer_state_t)1 << smart_layer_configs[i].layer);
        }
    }
    return state;
}
#endif
