// Copyright 2026 qmk-modules
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Smart Layer -- community module.
//
// NOTE: QMK force-includes every community module's config.h early, before the
// keyboard / user / keymap config.h. To keep every option overridable, this
// file intentionally defines nothing; all defaults and the SMART_LAYER_* switch
// guards live in smart_layer.h, which is included after config.h in each
// translation unit.
//
// Override from your keymap's config.h, for example:
//
//   #define SMART_LAYER_SLOT_COUNT 8
//   #define SMART_LAYER_DEFAULT_TIMEOUT 300
//   #define SMART_LAYER_SL_CHORDAL_HOLD 1
//   #define SMART_LAYER_SLT_FLOW_TAP 0

#pragma once
