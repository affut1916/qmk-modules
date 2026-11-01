// Copyright 2026 affut1916
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Smart OSM -- community module.
//
// QMK force-includes every community module's config.h early, before the
// keyboard / user / keymap config.h. To keep every option overridable, this
// file defines nothing; the defaults live in smart_osm.h behind #ifndef
// guards and can be overridden from your keymap's config.h:
//
//   #define SMART_OSM_TIMEOUT 500
//   #define SMART_OSM_QUEUE_TIMEOUT 0
//   #define SMART_OSM_LOCK 1

#pragma once
