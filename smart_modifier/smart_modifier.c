// Copyright 2026 affut1916
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Smart Modifier -- thin engine. See smart_modifier.h for the API.
//
// We never decide tap/hold ourselves. A trigger is an ordinary modifier or
// Mod-Tap keycode, so QMK core resolves it: for a Mod-Tap, `record->tap.count`
// tells us whether the hold half was used, and for a plain modifier the native
// press/release already is the whole behaviour. The module only adds the "keep
// the mod alive after the trigger is released" half.
//
// The mod is applied as weak mods. action_exec() clears weak mods on every
// press, so we re-add the active set on every event; that keeps the mod and the
// key it modifies in the same HID report instead of sending a bare modifier
// report first. The linger timeouts are therefore driven here, reading
// SMART_MODIFIER_TIMEOUT, instead of going through native One Shot handling.

#include QMK_KEYBOARD_H

#include "smart_modifier.h"

ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);

// ---- State -----------------------------------------------------------------

typedef struct {
    bool     active;
    bool     locked;        // ignores the timeout until the trigger is pressed again
    uint16_t keycode;       // the trigger that owns this slot
    uint8_t  mods;          // 8-bit report mask
    uint16_t timeout;       // ms; 0 disables the lingering
    uint16_t last_activity; // rolling window for the linger timeout
} smart_modifier_slot_t;

static smart_modifier_slot_t slots[SMART_MODIFIER_MAX_ACTIVE];

// Number of keys currently held (physical keys and combo outputs); while
// nonzero the linger timeouts are suspended so a long keypress is never cut
// short.
static uint16_t held_key_count = 0;

// Previous plain-modifier press, used only to detect a double tap. A press of
// any other key breaks the candidate, so a double tap means two presses of the
// same trigger with nothing in between.
static bool     tap_seen      = false;
static uint16_t last_tap_key  = 0;
static uint16_t last_tap_time = 0;

// ---- Helpers ---------------------------------------------------------------

// The 8-bit report mask of a plain modifier keycode, honouring the bootmagic
// remapping that native handling applies (keycode_config() then MOD_BIT()).
static uint8_t mask_from_mod_keycode(uint16_t keycode) {
    const uint16_t mapped = keycode_config(keycode);
    return IS_MODIFIER_KEYCODE(mapped) ? MOD_BIT(mapped) : 0;
}

// The 8-bit report mask of a Mod-Tap keycode: its packed mods run through the
// bootmagic mod swap, then unpacked from 5 to 8 bits exactly like QMK does
// (bit 0x10 is the right-hand flag, so a right-hand mod moves to the high
// nibble).
static uint8_t mask_from_mod_tap(uint16_t keycode) {
    uint8_t mods = mod_config(QK_MOD_TAP_GET_MODS(keycode));
    if (mods & 0x10) {
        mods <<= 4;
    }
    return mods;
}

static smart_modifier_slot_t *find_by_keycode(uint16_t keycode) {
    for (uint8_t i = 0; i < SMART_MODIFIER_MAX_ACTIVE; i++) {
        if (slots[i].active && slots[i].keycode == keycode) {
            return &slots[i];
        }
    }
    return NULL;
}

static smart_modifier_slot_t *find_free(void) {
    for (uint8_t i = 0; i < SMART_MODIFIER_MAX_ACTIVE; i++) {
        if (!slots[i].active) {
            return &slots[i];
        }
    }
    return NULL;
}

// Every mod currently owned by a slot.
static uint8_t active_mods(void) {
    uint8_t mods = 0;
    for (uint8_t i = 0; i < SMART_MODIFIER_MAX_ACTIVE; i++) {
        if (slots[i].active) {
            mods |= slots[i].mods;
        }
    }
    return mods;
}

// Re-applies the active weak mods. action_exec() clears weak mods before every
// press, so this is called on every event to keep the mod present.
static void apply(void) {
    const uint8_t mods = active_mods();
    if (mods) {
        add_weak_mods(mods);
    }
}

static void drop(const uint8_t mods) {
    if (mods) {
        del_weak_mods(mods);
        send_keyboard_report();
    }
}

// Releases the mods of `slot`, keeping any that another slot still owns.
static void deactivate(smart_modifier_slot_t *slot) {
    if (!slot->active) {
        return;
    }
    slot->active = false;
    drop(slot->mods & ~active_mods());
}

// Every press and release is activity: restart the linger windows so a long
// keypress does not expire the moment it is let go.
static void refresh_activity(void) {
    for (uint8_t i = 0; i < SMART_MODIFIER_MAX_ACTIVE; i++) {
        if (slots[i].active && !slots[i].locked) {
            slots[i].last_activity = timer_read();
        }
    }
}

// Starts lingering `mods` for `keycode`. Returns NULL when every slot is taken,
// in which case the trigger simply behaves natively.
static smart_modifier_slot_t *arm(uint16_t keycode, uint8_t mods, const smart_modifier_config_t *cfg) {
    smart_modifier_slot_t *slot = find_free();
    if (slot == NULL) {
        return NULL;
    }
    *slot = (smart_modifier_slot_t){
        .active        = true,
        .locked        = false,
        .keycode       = keycode,
        .mods          = mods,
        .timeout       = cfg->timeout,
        .last_activity = timer_read(),
    };
    return slot;
}

// Fills `cfg` with the defaults, then lets the keymap override them.
static smart_modifier_mode_t get_config(uint16_t keycode, smart_modifier_config_t *cfg) {
    cfg->timeout = SMART_MODIFIER_TIMEOUT;
    cfg->lock    = SMART_MODIFIER_LOCK;
    return smart_modifier_get(keycode, cfg);
}

// ---- Trigger handling ------------------------------------------------------

// A plain modifier press: lock, unlock, or arm/refresh. Acting on the press
// means the mod is armed for the whole press and the rolling window keeps
// running after the release; the press itself is left to native handling, so
// holding the key is a real modifier.
//
// Returns whether this press becomes the new double-tap candidate. An
// unlocking press must not, so that it cannot count into a new double tap.
static bool mod_press(uint16_t keycode, bool double_tap, const smart_modifier_config_t *cfg) {
    const uint8_t mods = mask_from_mod_keycode(keycode);
    if (mods == 0) {
        return false; // Not a modifier any more after keycode_config().
    }

    smart_modifier_slot_t *slot = find_by_keycode(keycode);
    if (slot == NULL) {
        return arm(keycode, mods, cfg) != NULL;
    }

    if (slot->locked) {
        // A locked mod is released by a press of the trigger that owns it.
        deactivate(slot);
        return false;
    }

    if (SMART_MODIFIER_LOCK && cfg->lock && double_tap) {
        slot->locked = true; // Double tap: lock.
        return false;
    }

    // Already lingering: refresh the window with this trigger's settings.
    slot->timeout       = cfg->timeout;
    slot->last_activity = timer_read();
    return true;
}

// A Mod-Tap hold release (tap.count == 0): the mod was really used, so it
// lingers. The tap half (tap.count > 0) is a native base key and never lingers.
static void mod_tap_release(uint16_t keycode, const smart_modifier_config_t *cfg) {
    const uint8_t mods = mask_from_mod_tap(keycode);
    if (mods == 0) {
        return;
    }

    smart_modifier_slot_t *slot = find_by_keycode(keycode);
    if (slot == NULL) {
        arm(keycode, mods, cfg);
        return;
    }
    slot->timeout       = cfg->timeout;
    slot->last_activity = timer_read();
}

// ---- Hooks -----------------------------------------------------------------

bool process_record_smart_modifier(uint16_t keycode, keyrecord_t *record) {
    if (!process_record_smart_modifier_kb(keycode, record)) {
        return false;
    }

    const bool pressed = record->event.pressed;

    smart_modifier_config_t     cfg;
    const smart_modifier_mode_t mode = get_config(keycode, &cfg);

    if (pressed) {
        // A double tap is two presses of the same trigger within the usual quick
        // tap term, with no other key press in between. Any press breaks the
        // previous candidate; the trigger below starts a new one.
        const bool double_tap = tap_seen && last_tap_key == keycode && timer_elapsed(last_tap_time) < GET_QUICK_TAP_TERM(keycode, record);
        tap_seen              = false;

        if (mode == SMART_MODIFIER_MOD && mod_press(keycode, double_tap, &cfg)) {
            tap_seen      = true;
            last_tap_key  = keycode;
            last_tap_time = timer_read();
        }
    } else if (mode == SMART_MODIFIER_MOD_TAP && record->tap.count == 0) {
        mod_tap_release(keycode, &cfg);
    }

    // Physical keys and combo outputs both count as held, so a long keypress (or
    // a held combo) suspends the linger timeouts.
    if (IS_KEYEVENT(record->event) || IS_COMBOEVENT(record->event)) {
        if (pressed) {
            held_key_count++;
        } else if (held_key_count > 0) {
            held_key_count--;
        }
    }

    // The mod is applied as weak mods; action_exec() clears weak mods on every
    // press, so re-add the active set on every event (this is idempotent).
    apply();

    // A release is activity too: keep the linger windows rolling.
    refresh_activity();

    return true;
}

void housekeeping_task_smart_modifier(void) {
    housekeeping_task_smart_modifier_kb();

    if (held_key_count > 0) {
        return; // Suspended while any key is held.
    }

    for (uint8_t i = 0; i < SMART_MODIFIER_MAX_ACTIVE; i++) {
        smart_modifier_slot_t *slot = &slots[i];
        // A timeout of 0 means "no lingering": the slot is released as soon as
        // the trigger is no longer held (the held-key check above has already
        // passed), i.e. the mod is active only while the trigger is held.
        if (slot->active && !slot->locked && (slot->timeout == 0 || timer_elapsed(slot->last_activity) >= slot->timeout)) {
            deactivate(slot);
        }
    }
}

// ---- Weak defaults ---------------------------------------------------------

__attribute__((weak)) smart_modifier_mode_t smart_modifier_get(uint16_t keycode, smart_modifier_config_t *cfg) {
    (void)keycode;
    (void)cfg;
    return SMART_MODIFIER_NONE;
}
