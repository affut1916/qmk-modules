// Copyright 2026 affut1916
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Smart Modifier -- community module.
//
// QMK force-includes every community module's config.h early, before the
// keyboard / user / keymap config.h. To keep every option overridable, this
// file defines nothing; the defaults live in smart_modifier.h behind #ifndef
// guards and can be overridden from your keymap's config.h:
//
//   #define SMART_MODIFIER_TIMEOUT 250
//   #define SMART_MODIFIER_QUEUE_TIMEOUT 0
//   #define SMART_MODIFIER_LOCK 1

#pragma once
