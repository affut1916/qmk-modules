// Copyright 2026 qmk-modules
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Smart Layer -- community module.
//
// QMK force-includes every community module's config.h early, before the
// keyboard / user / keymap config.h. To keep every option overridable, this
// file defines nothing; the defaults live in smart_layer.h behind #ifndef
// guards and can be overridden from your keymap's config.h:
//
//   #define SMART_LAYER_MAX_ACTIVE 8
//   #define SMART_LAYER_DEFAULT_TIMEOUT 300
//   #define SMART_LAYER_SWALLOW_EXIT 0
//   #define SMART_LAYER_SL_DOUBLE_TAP_LOCK 1

#pragma once
