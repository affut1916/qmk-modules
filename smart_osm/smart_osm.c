// Copyright 2026 affut1916
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Smart OSM -- a smarter One Shot Modifier. See smart_osm.h for the
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
// SMART_OSM_TIMEOUT and SMART_OSM_QUEUE_TIMEOUT instead.

#include QMK_KEYBOARD_H

#include "smart_osm.h"

ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);

#ifdef NO_ACTION_ONESHOT
#    error "Smart OSM triggers are OSM keys and rely on native One Shot Modifiers. Please undefine NO_ACTION_ONESHOT."
#endif

// ---- State -----------------------------------------------------------------

// Armed but not yet applied: waiting for the first ordinary key. Distinct from
// `active` so a repeat tap can tell "not used yet" from "already used", which
// is what makes toggling a mod off reliable even within the double-tap window.
static uint8_t so_queued = 0;
// Currently applied, as weak mods. Kept separate from any physically held mods
// so the timeout only ever clears the ones we own.
static uint8_t so_active = 0;

static bool so_locked = false;

// Per-trigger settings, captured from the trigger that armed the current mods.
static uint16_t so_timeout       = SMART_OSM_TIMEOUT;
static uint16_t so_queue_timeout = SMART_OSM_QUEUE_TIMEOUT;

static uint16_t so_armed_time    = 0; // when the mods were queued, for the queue timeout
static uint16_t so_last_activity = 0; // rolling window for the linger timeout

// Previous trigger tap, used only to detect a double tap.
static bool     so_tap_seen      = false;
static uint16_t so_last_tap_key  = 0;
static uint16_t so_last_tap_time = 0;

// Number of physical keys currently held; while nonzero the timers are
// suspended so a long keypress is never cut short.
static uint16_t so_held_keys = 0;

// ---- Helpers ---------------------------------------------------------------

// Unpacks the 5-bit mods of an OSM keycode into QMK's 8-bit representation,
// honouring the swap/no-GUI bootmagic settings.
static uint8_t so_unpack_mods(uint16_t keycode) {
    uint8_t mods = mod_config(QK_ONE_SHOT_MOD_GET_MODS(keycode));
    if (mods & 0x10) { // Right-hand mod bit: expand to the 8-bit mask.
        mods <<= 4;
    }
    return mods;
}

// Re-applies the active weak mods. action_exec() clears weak mods before every
// press, so this runs on each ordinary press to keep the mod present.
static void so_apply(void) {
    if (so_active) {
        add_weak_mods(so_active);
    }
}

static void so_drop(const uint8_t mods) {
    if (mods) {
        del_weak_mods(mods);
        send_keyboard_report();
    }
}

static void so_clear(void) {
    so_drop(so_active);
    so_queued        = 0;
    so_active        = 0;
    so_locked        = false;
    so_armed_time    = 0;
    so_last_activity = 0;
    so_tap_seen      = false; // Don't let the unlocking tap count into a new double tap.
}

// Whether a press produces a key in the HID report, and so consumes the mod.
// Mirrors native OSM, where a queued mod is only cleared once an actual key is
// added to the report: real modifiers and layer switches do not consume it.
static bool so_consumes(uint16_t keycode, keyrecord_t *record) {
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
static smart_osm_mode_t so_get_config(uint16_t keycode, smart_osm_config_t *cfg) {
    cfg->timeout       = SMART_OSM_TIMEOUT;
    cfg->queue_timeout = SMART_OSM_QUEUE_TIMEOUT;
    return smart_osm_get(keycode, cfg);
}

// ---- Trigger handling ------------------------------------------------------

// A tap of an opted-in OSM key: lock, unlock, cancel, or arm the mod. Only
// called on the press event, so a fast double tap is caught here (a tap can be
// counted even before its release is processed).
static void so_tap(uint16_t keycode, keyrecord_t *record, const smart_osm_config_t *cfg) {
    const uint8_t mods = so_unpack_mods(keycode);

    // A double tap is two taps of the same trigger within the usual quick tap
    // term. Same rule as a regular tap-hold key.
    const bool double_tap = so_tap_seen && so_last_tap_key == keycode && timer_elapsed(so_last_tap_time) < GET_QUICK_TAP_TERM(keycode, record);
    so_tap_seen      = true;
    so_last_tap_key  = keycode;
    so_last_tap_time = timer_read();

    if (so_locked && (so_active & mods)) {
        // A locked mod is released by a tap of the trigger that owns it. A tap
        // of another trigger still arms or cancels its own mod below.
        so_clear();
        return;
    }

    if (SMART_OSM_LOCK && double_tap) {
        // Lock the mod and make sure it is applied.
        so_queued &= ~mods;
        so_active |= mods;
        so_apply();
        so_locked        = true;
        so_last_activity = timer_read();
        return;
    }

    if ((so_queued | so_active) & mods) {
        // Already armed or applied: a tap cancels the mod.
        so_drop(so_active & mods);
        so_queued &= ~mods;
        so_active &= ~mods;
        if (!so_queued && !so_active) {
            so_armed_time    = 0;
            so_last_activity = 0;
        }
        return;
    }

    // Arm the mod, combining with other queued triggers so the mods stack.
    so_queued |= mods;
    so_timeout       = cfg->timeout;
    so_queue_timeout = cfg->queue_timeout;
    so_armed_time    = timer_read();
    if (so_active) {
        // A mod from another trigger is already applied; apply this one too so
        // the combination works right away.
        so_active |= mods;
        so_apply();
        so_last_activity = timer_read();
    }
}

// ---- Hooks -----------------------------------------------------------------

bool process_record_smart_osm(uint16_t keycode, keyrecord_t *record) {
    if (!process_record_smart_osm_kb(keycode, record)) {
        return false;
    }

    const bool pressed = record->event.pressed;

    // Triggers are plain OSM keys the keymap opted in.
    if (IS_QK_ONE_SHOT_MOD(keycode) && is_oneshot_enabled()) {
        smart_osm_config_t cfg;
        if (so_get_config(keycode, &cfg) == SMART_OSM_OSM && record->tap.count > 0) {
            if (pressed) {
                so_tap(keycode, record, &cfg);
            }
            // Claim both halves of the tap so native OSM never queues the mod.
            return false;
        }
        // Hold (tap.count == 0), or an OSM key that did not opt in: native.
    }

    if (IS_KEYEVENT(record->event)) {
        if (pressed) {
            so_held_keys++;
        } else {
            if (so_held_keys > 0) {
                so_held_keys--;
            }
            // A release is activity too: keep the linger window rolling.
            if (so_active && !so_locked) {
                so_last_activity = timer_read();
            }
        }
    }

    if (pressed && so_consumes(keycode, record)) {
        // Any other key press breaks a potential double tap, like native
        // tap-hold interruption.
        so_tap_seen = false;
        if (so_queued) {
            so_active |= so_queued;
            so_queued = 0;
        }
        if (so_active) {
            // An ordinary key: re-add weak mods so the mod and the key go out in
            // the same report.
            so_apply();
            if (!so_locked) {
                so_last_activity = timer_read();
            }
        }
    } else if (pressed) {
        // A modifier or layer key does not consume the mod, but is still
        // activity: you may hold Ctrl and still use the armed Shift.
        so_tap_seen = false;
        if (so_active) {
            so_apply();
            if (!so_locked) {
                so_last_activity = timer_read();
            }
        }
    }

    return true;
}

void housekeeping_task_smart_osm(void) {
    housekeeping_task_smart_osm_kb();

    if (so_held_keys > 0 || so_locked) {
        return; // Suspended while a key is held, or while locked.
    }

    if (so_queued && so_queue_timeout != 0 && timer_elapsed(so_armed_time) >= so_queue_timeout) {
        so_queued = 0;
    }

    if (so_active && so_timeout != 0 && timer_elapsed(so_last_activity) >= so_timeout) {
        so_clear();
    }
}

// ---- Weak defaults ---------------------------------------------------------

__attribute__((weak)) smart_osm_mode_t smart_osm_get(uint16_t keycode, smart_osm_config_t *cfg) {
    (void)keycode;
    (void)cfg;
    return SMART_OSM_NONE;
}
