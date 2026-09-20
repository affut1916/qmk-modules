// Copyright 2026 qmk-modules
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Smart Layer -- thin engine. See smart_layer.h for the API.
//
// We never implement tap/hold ourselves: QMK resolves every LT before this
// module sees it, and `record->tap.count` tells us which half fired. We only
// rewrite the half we own and leave the other half to native handling.
//
//   SL  = LT(layer, KC_NO)   tap  -> sticky layer, hold -> native MO
//   SLT = LT(layer, kc)      tap  -> native kc,    hold -> sticky layer

#include QMK_KEYBOARD_H

#include "smart_layer.h"

#ifdef CAPS_WORD_ENABLE
#    include "process_caps_word.h"
#    include "caps_word.h"
#endif

ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 1, 0);

typedef struct {
    bool            active;
    bool            locked;        // ignores timeout and the whitelist
    uint8_t         layer;
    keypos_t        trigger_pos;   // physical position of the trigger
    uint16_t        timeout;       // ms; 0 disables the idle timeout
    uint16_t        tap_time;      // time of the last tap, for SL double-tap
    uint16_t        last_activity; // for the idle timeout
    bool            swallow_exit;
    const uint16_t *continue_list;
    uint8_t         continue_list_size;
} smart_active_t;

static smart_active_t active[SMART_LAYER_MAX_ACTIVE];

// Number of physical keys currently held; while nonzero the idle timeout is
// suspended so that a long keypress never cuts a sticky layer short.
static uint16_t held_key_count = 0;

// A key that triggered an automatic exit and is being swallowed.
static bool     swallow_active = false;
static keypos_t swallowed_key  = {0};

// ---- Helpers ---------------------------------------------------------------

// Matches an active trigger by its physical key position. Position (rather
// than keycode) is used so it keeps working after the layer is on and the
// position may resolve to a different keycode.
static smart_active_t *find_by_pos(keypos_t pos) {
    for (uint8_t i = 0; i < SMART_LAYER_MAX_ACTIVE; i++) {
        if (active[i].active && KEYEQ(active[i].trigger_pos, pos)) {
            return &active[i];
        }
    }
    return NULL;
}

static smart_active_t *find_free(void) {
    for (uint8_t i = 0; i < SMART_LAYER_MAX_ACTIVE; i++) {
        if (!active[i].active) {
            return &active[i];
        }
    }
    return NULL;
}

static bool other_active_on_layer(uint8_t layer, const smart_active_t *exclude) {
    for (uint8_t i = 0; i < SMART_LAYER_MAX_ACTIVE; i++) {
        if (&active[i] != exclude && active[i].active && active[i].layer == layer) {
            return true;
        }
    }
    return false;
}

static void deactivate(smart_active_t *slot) {
    if (!slot->active) {
        return;
    }
    const uint8_t layer = slot->layer;
    slot->active = false;
    if (!other_active_on_layer(layer, slot)) {
        layer_off(layer);
    }
}

static void deactivate_all(void) {
    for (uint8_t i = 0; i < SMART_LAYER_MAX_ACTIVE; i++) {
        deactivate(&active[i]);
    }
}

static bool continue_list_match(const smart_active_t *slot, uint16_t keycode) {
    for (uint8_t i = 0; i < slot->continue_list_size; i++) {
        if (slot->continue_list[i] == keycode) {
            return true;
        }
    }
    return false;
}

// True if the key keeps the layer on: either listed in continue_list, or
// defined (non-transparent) on the target layer at that position.
static bool keeps_layer_on(const smart_active_t *slot, uint16_t keycode, keyrecord_t *record) {
    if (continue_list_match(slot, keycode)) {
        return true;
    }
    if (IS_KEYEVENT(record->event) && slot->layer < MAX_LAYER) {
        uint16_t on_layer = keymap_key_to_keycode(slot->layer, record->event.key);
        if (on_layer != KC_TRANSPARENT && on_layer != KC_NO) {
            return true;
        }
    }
    return false;
}

// Fills `cfg` with the defaults, then lets the keymap override them.
static smart_layer_mode_t get_config(uint16_t keycode, smart_layer_config_t *cfg) {
    cfg->timeout            = SMART_LAYER_DEFAULT_TIMEOUT;
    cfg->swallow_exit       = SMART_LAYER_SWALLOW_EXIT;
    cfg->continue_list      = NULL;
    cfg->continue_list_size = 0;
    return smart_layer_get(keycode, cfg);
}

// Creates a sticky layer for `keycode`. Returns NULL if the layer is invalid or
// no slot is free.
static smart_active_t *activate(uint16_t keycode, keypos_t pos, const smart_layer_config_t *cfg) {
    const uint8_t layer = QK_LAYER_TAP_GET_LAYER(keycode);
    if (layer >= MAX_LAYER) {
        return NULL;
    }
    smart_active_t *slot = find_free();
    if (slot == NULL) {
        return NULL;
    }
    *slot = (smart_active_t){
        .active             = true,
        .locked             = false,
        .layer              = layer,
        .trigger_pos        = pos,
        .timeout            = cfg->timeout,
        .tap_time           = timer_read(),
        .last_activity      = timer_read(),
        .swallow_exit       = cfg->swallow_exit,
        .continue_list      = cfg->continue_list,
        .continue_list_size = cfg->continue_list_size,
    };
    layer_on(layer);
    return slot;
}

// ---- Trigger handling ------------------------------------------------------

// SL tap (tap.count > 0 on press): activate / toggle off / lock / unlock.
static void sl_tap(uint16_t keycode, keyrecord_t *record) {
    smart_layer_config_t cfg;
    (void)get_config(keycode, &cfg);

    smart_active_t *slot = find_by_pos(record->event.key);
    if (slot == NULL) {
        activate(keycode, record->event.key, &cfg);
        return;
    }
    if (slot->locked) {
        deactivate(slot); // Tap again to unlock (and turn the layer off).
        return;
    }
    if (SMART_LAYER_SL_DOUBLE_TAP_LOCK && timer_elapsed(slot->tap_time) < GET_QUICK_TAP_TERM(keycode, record)) {
        slot->locked = true; // Double tap: lock.
    } else {
        deactivate(slot); // Toggle off.
    }
    slot->tap_time = timer_read();
}

// SLT hold (tap.count == 0 on press): activate, or close if already active.
static void slt_hold(uint16_t keycode, keyrecord_t *record) {
    smart_active_t *slot = find_by_pos(record->event.key);
    if (slot != NULL) {
        deactivate(slot); // Repeat hold closes it.
        return;
    }
    smart_layer_config_t cfg;
    (void)get_config(keycode, &cfg);
    activate(keycode, record->event.key, &cfg);
}

// ---- Auto-exit -------------------------------------------------------------

// Called on every key press. Closes sticky layers the key does not belong to.
// Returns true if the event should be swallowed.
static bool check_auto_exit(uint16_t keycode, keyrecord_t *record) {
    // Non-key events (combos, macros) never exit a layer.
    if (!IS_KEYEVENT(record->event)) {
        return false;
    }

    bool swallow = false;
    for (uint8_t i = 0; i < SMART_LAYER_MAX_ACTIVE; i++) {
        smart_active_t *slot = &active[i];
        if (!slot->active || slot->locked) {
            continue;
        }
        // A trigger never exits the layer it belongs to.
        if (KEYEQ(slot->trigger_pos, record->event.key)) {
            slot->last_activity = timer_read();
            continue;
        }
        if (keeps_layer_on(slot, keycode, record)) {
            slot->last_activity = timer_read();
            continue;
        }
        if (slot->swallow_exit) {
            swallow = true;
        }
        deactivate(slot);
    }
    return swallow;
}

// ---- Hooks -----------------------------------------------------------------

bool process_record_smart_layer(uint16_t keycode, keyrecord_t *record) {
    if (!process_record_smart_layer_kb(keycode, record)) {
        return false;
    }

    const bool pressed = record->event.pressed;

    // Swallow the release of a key that triggered an automatic exit. Its press
    // was already counted in held_key_count, so undo that here.
    if (swallow_active && !pressed && IS_KEYEVENT(record->event) && KEYEQ(record->event.key, swallowed_key)) {
        swallow_active = false;
        if (held_key_count > 0) {
            held_key_count--;
        }
        return false;
    }

    if (keycode == SL_OFF) {
        if (pressed) {
            deactivate_all();
#ifdef CAPS_WORD_ENABLE
            caps_word_off();
#endif
        }
        return false;
    }

    if (IS_KEYEVENT(record->event)) {
        if (pressed) {
            held_key_count++;
        } else if (held_key_count > 0) {
            held_key_count--;
        }
    }

    // A trigger is any Layer-Tap key the keymap recognises.
    if (IS_QK_LAYER_TAP(keycode)) {
        smart_layer_config_t cfg;
        const smart_layer_mode_t mode = get_config(keycode, &cfg);

        if (mode == SMART_LAYER_SL) {
            if (record->tap.count > 0) {
                if (pressed) {
                    sl_tap(keycode, record);
                }
                return false; // Tap handled here.
            }
            // Hold: native MO. Like the other layer-activation halves, this is
            // not an ordinary press, so it does not run the auto-exit pass.
            return true;
        } else if (mode == SMART_LAYER_SLT) {
            if (record->tap.count == 0) {
                if (pressed) {
                    slt_hold(keycode, record);
                } else {
                    // Start the idle timeout from the trigger's release.
                    smart_active_t *slot = find_by_pos(record->event.key);
                    if (slot != NULL) {
                        slot->last_activity = timer_read();
                        slot->tap_time      = timer_read();
                    }
                }
                return false; // Hold handled here; tap falls through to native kc.
            }
        }
    }

    if (pressed && check_auto_exit(keycode, record)) {
        swallow_active = true;
        swallowed_key  = record->event.key;
        return false;
    }

    return true;
}

void housekeeping_task_smart_layer(void) {
    housekeeping_task_smart_layer_kb();
    if (held_key_count > 0) {
        return; // Suspend the idle timeout while any key is held.
    }
    for (uint8_t i = 0; i < SMART_LAYER_MAX_ACTIVE; i++) {
        smart_active_t *slot = &active[i];
        if (slot->active && !slot->locked && slot->timeout != 0 &&
            timer_elapsed(slot->last_activity) >= slot->timeout) {
            deactivate(slot);
        }
    }
}

#if !defined(NO_ACTION_LAYER)
layer_state_t layer_state_set_smart_layer(layer_state_t state) {
    state = layer_state_set_smart_layer_kb(state);
    for (uint8_t i = 0; i < SMART_LAYER_MAX_ACTIVE; i++) {
        if (active[i].active && active[i].layer < MAX_LAYER) {
            state |= ((layer_state_t)1 << active[i].layer);
        }
    }
    return state;
}
#endif

// ---- Weak defaults ---------------------------------------------------------

__attribute__((weak)) smart_layer_mode_t smart_layer_get(uint16_t keycode, smart_layer_config_t *cfg) {
    (void)keycode;
    (void)cfg;
    return SMART_LAYER_NONE;
}
