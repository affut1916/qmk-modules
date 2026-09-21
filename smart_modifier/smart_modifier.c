// Copyright 2026 affut1916
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Smart Modifier -- a smarter One Shot Modifier. See smart_modifier.h for the
// API and the full behaviour description.
//
// We never decide tap/hold ourselves. The trigger is a plain OSM(mod) key, so
// QMK's action_tapping.c settles it and `record->tap.count` tells us which half
// fired. We only claim the tap half and leave the hold half to native OSM.
//
// The mod is applied as weak mods. action_exec() clears weak mods on every
// press, so we re-add the active set on each ordinary key press; that keeps the
// mod and the key it modifies in the same HID report instead of sending a bare
// modifier report first. The tap half never reaches native OSM, so we cannot
// use add_oneshot_mods(); the timeouts are therefore driven here, reading
// SMART_MODIFIER_TIMEOUT and SMART_MODIFIER_QUEUE_TIMEOUT instead.

#include QMK_KEYBOARD_H

#include "smart_modifier.h"

ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);

#ifdef NO_ACTION_ONESHOT
#    error "Smart Modifier triggers are OSM keys and rely on native One Shot Modifiers. Please undefine NO_ACTION_ONESHOT."
#endif

// ---- State -----------------------------------------------------------------

// Armed but not yet applied: waiting for the first ordinary key. Distinct from
// `active` so a repeat tap can tell "not used yet" from "already used", which
// is what makes toggling a mod off reliable even within the double-tap window.
static uint8_t sm_queued = 0;
// Currently applied, as weak mods. Kept separate from any physically held mods
// so the timeout only ever clears the ones we own.
static uint8_t sm_active = 0;

static bool sm_locked = false;

// Per-trigger settings, captured from the trigger that armed the current mods.
static uint16_t sm_timeout       = SMART_MODIFIER_TIMEOUT;
static uint16_t sm_queue_timeout = SMART_MODIFIER_QUEUE_TIMEOUT;

static uint16_t sm_armed_time    = 0; // when the mods were queued, for the queue timeout
static uint16_t sm_last_activity = 0; // rolling window for the linger timeout

// Previous trigger tap, used only to detect a double tap.
static bool     sm_tap_seen      = false;
static uint16_t sm_last_tap_key  = 0;
static uint16_t sm_last_tap_time = 0;

// Number of physical keys currently held; while nonzero the timers are
// suspended so a long keypress is never cut short.
static uint16_t sm_held_keys = 0;

// ---- Helpers ---------------------------------------------------------------

// Unpacks the 5-bit mods of an OSM keycode into QMK's 8-bit representation,
// honouring the swap/no-GUI bootmagic settings.
static uint8_t sm_unpack_mods(uint16_t keycode) {
    uint8_t mods = mod_config(QK_ONE_SHOT_MOD_GET_MODS(keycode));
    if (mods & 0x10) { // Right-hand mod bit: expand to the 8-bit mask.
        mods <<= 4;
    }
    return mods;
}

// Re-applies the active weak mods. action_exec() clears weak mods before every
// press, so this runs on each ordinary press to keep the mod present.
static void sm_apply(void) {
    if (sm_active) {
        add_weak_mods(sm_active);
    }
}

static void sm_drop(const uint8_t mods) {
    if (mods) {
        del_weak_mods(mods);
        send_keyboard_report();
    }
}

static void sm_clear(void) {
    sm_drop(sm_active);
    sm_queued        = 0;
    sm_active        = 0;
    sm_locked        = false;
    sm_armed_time    = 0;
    sm_last_activity = 0;
    sm_tap_seen      = false; // Don't let the unlocking tap count into a new double tap.
}

// Whether a press produces a key in the HID report, and so consumes the mod.
// Mirrors native OSM, where a queued mod is only cleared once an actual key is
// added to the report: real modifiers and layer switches do not consume it.
static bool sm_consumes(uint16_t keycode, keyrecord_t *record) {
    if (IS_MODIFIER_KEYCODE(keycode)) {
        return false;
    }
    if (IS_QK_ONE_SHOT_MOD(keycode) || IS_QK_ONE_SHOT_LAYER(keycode)) {
        return false;
    }
    if (IS_QK_MOMENTARY(keycode) || IS_QK_LAYER_TAP_TOGGLE(keycode) || IS_QK_LAYER_MOD(keycode)) {
        return false;
    }
    if (keycode >= QK_TO && keycode <= QK_TO_MAX) {
        return false;
    }
    if (keycode >= QK_TOGGLE_LAYER && keycode <= QK_TOGGLE_LAYER_MAX) {
        return false;
    }
#ifdef TRI_LAYER_ENABLE
    if (keycode >= QK_TRI_LAYER_LOWER && keycode <= QK_TRI_LAYER_UPPER) {
        return false;
    }
#endif
    // A mod-tap or layer-tap only acts as a regular key when it taps.
    if (record->tap.count == 0 && (IS_QK_MOD_TAP(keycode) || IS_QK_LAYER_TAP(keycode))) {
        return false;
    }
    return true;
}

// Fills `cfg` with the defaults, then lets the keymap override them.
static smart_modifier_mode_t sm_get_config(uint16_t keycode, smart_modifier_config_t *cfg) {
    cfg->timeout       = SMART_MODIFIER_TIMEOUT;
    cfg->queue_timeout = SMART_MODIFIER_QUEUE_TIMEOUT;
    return smart_modifier_get(keycode, cfg);
}

// ---- Trigger handling ------------------------------------------------------

// A tap of an opted-in OSM key: lock, unlock, cancel, or arm the mod. Only
// called on the press event, so a fast double tap is caught here (a tap can be
// counted even before its release is processed).
static void sm_tap(uint16_t keycode, keyrecord_t *record, const smart_modifier_config_t *cfg) {
    const uint8_t mods = sm_unpack_mods(keycode);

    // A double tap is two taps of the same trigger within the usual quick tap
    // term. Same rule as a regular tap-hold key.
    const bool double_tap = sm_tap_seen && sm_last_tap_key == keycode && timer_elapsed(sm_last_tap_time) < GET_QUICK_TAP_TERM(keycode, record);
    sm_tap_seen      = true;
    sm_last_tap_key  = keycode;
    sm_last_tap_time = timer_read();

    if (sm_locked && (sm_active & mods)) {
        // A locked mod is released by a tap of the trigger that owns it. A tap
        // of another trigger still arms or cancels its own mod below.
        sm_clear();
        return;
    }

    if (SMART_MODIFIER_LOCK && double_tap) {
        // Lock the mod and make sure it is applied.
        sm_queued &= ~mods;
        sm_active |= mods;
        sm_apply();
        sm_locked        = true;
        sm_last_activity = timer_read();
        return;
    }

    if ((sm_queued | sm_active) & mods) {
        // Already armed or applied: a tap cancels the mod.
        sm_drop(sm_active & mods);
        sm_queued &= ~mods;
        sm_active &= ~mods;
        if (!sm_queued && !sm_active) {
            sm_armed_time    = 0;
            sm_last_activity = 0;
        }
        return;
    }

    // Arm the mod, combining with other queued triggers so the mods stack.
    sm_queued |= mods;
    sm_timeout       = cfg->timeout;
    sm_queue_timeout = cfg->queue_timeout;
    sm_armed_time    = timer_read();
    if (sm_active) {
        // A mod from another trigger is already applied; apply this one too so
        // the combination works right away.
        sm_active |= mods;
        sm_apply();
        sm_last_activity = timer_read();
    }
}

// ---- Hooks -----------------------------------------------------------------

bool process_record_smart_modifier(uint16_t keycode, keyrecord_t *record) {
    if (!process_record_smart_modifier_kb(keycode, record)) {
        return false;
    }

    const bool pressed = record->event.pressed;

    // Triggers are plain OSM keys the keymap opted in.
    if (IS_QK_ONE_SHOT_MOD(keycode) && is_oneshot_enabled()) {
        smart_modifier_config_t cfg;
        if (sm_get_config(keycode, &cfg) == SMART_MODIFIER_OSM && record->tap.count > 0) {
            if (pressed) {
                sm_tap(keycode, record, &cfg);
            }
            // Claim both halves of the tap so native OSM never queues the mod.
            return false;
        }
        // Hold (tap.count == 0), or an OSM key that did not opt in: native.
    }

    if (IS_KEYEVENT(record->event)) {
        if (pressed) {
            sm_held_keys++;
        } else {
            if (sm_held_keys > 0) {
                sm_held_keys--;
            }
            // A release is activity too: keep the linger window rolling.
            if (sm_active && !sm_locked) {
                sm_last_activity = timer_read();
            }
        }
    }

    if (pressed && sm_consumes(keycode, record)) {
        // Any other key press breaks a potential double tap, like native
        // tap-hold interruption.
        sm_tap_seen = false;
        if (sm_queued) {
            sm_active |= sm_queued;
            sm_queued = 0;
        }
        if (sm_active) {
            // An ordinary key: re-add weak mods so the mod and the key go out in
            // the same report.
            sm_apply();
            if (!sm_locked) {
                sm_last_activity = timer_read();
            }
        }
    } else if (pressed) {
        // A modifier or layer key does not consume the mod, but is still
        // activity: you may hold Ctrl and still use the armed Shift.
        sm_tap_seen = false;
        if (sm_active) {
            sm_apply();
            if (!sm_locked) {
                sm_last_activity = timer_read();
            }
        }
    }

    return true;
}

void housekeeping_task_smart_modifier(void) {
    housekeeping_task_smart_modifier_kb();

    if (sm_held_keys > 0 || sm_locked) {
        return; // Suspended while a key is held, or while locked.
    }

    if (sm_queued && sm_queue_timeout != 0 && timer_elapsed(sm_armed_time) >= sm_queue_timeout) {
        sm_queued = 0;
    }

    if (sm_active && sm_timeout != 0 && timer_elapsed(sm_last_activity) >= sm_timeout) {
        sm_clear();
    }
}

// ---- Weak defaults ---------------------------------------------------------

__attribute__((weak)) smart_modifier_mode_t smart_modifier_get(uint16_t keycode, smart_modifier_config_t *cfg) {
    (void)keycode;
    (void)cfg;
    return SMART_MODIFIER_NONE;
}
