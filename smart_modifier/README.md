# smart_modifier

> 中文 | [English](#english)

QMK 社区模块「智能 Modifier」：让普通 modifier（如 `KC_LSFT`）和 Mod-Tap（如 `LGUI_T(KC_I)`）的 mod 在松手后继续有效一小段时间。

> **免责声明**：本模块完全由 AI 生成，未经实机编译与测试，代码质量不作任何保证。使用前请自行审阅源码，并自行承担风险。

---

## 它解决什么问题

普通 `KC_LSFT` / `KC_RCTL` 只在按住期间有效；Mod-Tap 的 mod 半边也一样，松手就没。

典型场景：按住 Shift 连打几个字母，在最后一个字母抬起前 Shift 已经松开了，结果最后一个字母变小写；或者按住 GUI 点了一个键，想再点一个还得重新按住。

本模块让 mod **多留一会儿**：触发键（普通 modifier 键，或 Mod-Tap 的 hold 半边）松手之后，mod 继续有效一小段时间（默认 1000ms，滚动窗口），这一串快速按键都能带上修饰键。

---

## 行为

只对回调里声明过的键生效；其它键（包括其它 modifier、其它 Mod-Tap、OSM、LT 等）完全保持原生。

| 触发类型 | 轻点 tap | 按住 hold | 松手后 | 双击 |
|---|---|---|---|---|
| 普通 modifier `SMART_MODIFIER_MOD` | 与原生一致（一次 modifier 轻点），同时武装滞留 | 原生真实 modifier | mod 继续有效 `timeout` 毫秒 | 锁定该 mod（`SMART_MODIFIER_LOCK`，默认开）；再按一次同一触发键解锁 |
| Mod-Tap `SMART_MODIFIER_MOD_TAP` | 原生基础键（如 `i`），**不**留 mod | 原生 modifier | mod 继续有效 `timeout` 毫秒 | 无锁定 |

- **滚动窗口**：每次按下/抬起都重置计时；只要有任意键按住，计时暂停，长按不会被截断。
- **多个触发键可叠加**：滞留的 Shift + 滞留的 Ctrl，下一个键同时带 Shift+Ctrl；每个触发键的 `timeout` 与锁定状态互相独立。
- **tap/hold 判定完全交给 QMK 原生**：触发键就是普通的 modifier / Mod-Tap 键码，所以 `PERMISSIVE_HOLD`、`HOLD_ON_OTHER_KEY_PRESS`、`QUICK_TAP_TERM`、`CHORDAL_HOLD`、`get_tapping_term()` 等全部原生生效。
- **不拦截触发键本身**：按下/抬起照常交给原生处理（bootmagic 的 mod 交换、`MAGIC_ENABLE` 等行为一致），模块只在 weak mods 里补一份「滞留」。

### 超时

- **滞留超时** `SMART_MODIFIER_TIMEOUT`（默认 1000ms）：触发键松手后 mod 继续有效的时长；`0` = 不滞留（松手即失效，双击锁定不受影响）。
- 每个键都能在回调里用 `cfg->timeout` 单独覆盖，粒度到触发键。

---

## 安装

### 1. 放置模块

把 `smart_modifier/` 放到 QMK userspace 或 firmware 的 `modules/affut1916` 目录下。

### 2. 在 keymap.json 中启用

```json
{
    "modules": ["affut1916/smart_modifier"]
}
```

---

## 快速上手

```c
#include QMK_KEYBOARD_H
#include "smart_modifier.h"

smart_modifier_mode_t smart_modifier_get(uint16_t keycode, smart_modifier_config_t *cfg) {
    // 进入回调时 cfg 已填好默认值，只改你需要改的。
    switch (keycode) {
        case KC_LSFT:
            return SMART_MODIFIER_MOD;      // 默认 1000ms，双击锁定

        case KC_RCTL:
            cfg->timeout = 500;             // 覆盖默认 1000ms
            return SMART_MODIFIER_MOD;

        case LGUI_T(KC_I):
            return SMART_MODIFIER_MOD_TAP;  // 轻点出 i，按住是 GUI
    }
    return SMART_MODIFIER_NONE; // 其它键保持原生
}
```

键位图里照常写普通键码，没声明的键完全原生：

```c
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT(
        /* ... */
        KC_LSFT, KC_RCTL, LGUI_T(KC_I), KC_LALT  // KC_LALT 未声明 = 纯原生
    ),
};
```

完整示例见 [`example/keymap.c`](example/keymap.c)。

---

## 配置

### 回调中的 `smart_modifier_config_t`

进入 `smart_modifier_get()` 时 `cfg` 已填入下列默认值：

| 字段 | 默认 | 说明 |
|---|---|---|
| `timeout` | `SMART_MODIFIER_TIMEOUT` (1000) | 滞留超时，毫秒；`0` = 不滞留（双击锁定不受影响） |
| `lock` | `SMART_MODIFIER_LOCK` (1) | 该键是否允许双击锁定（仅普通 modifier 有效） |

### config.h 全局宏

| 宏 | 默认 | 说明 |
|---|---|---|
| `SMART_MODIFIER_TIMEOUT` | `1000` | 默认滞留超时（毫秒）；`0` = 不滞留 |
| `SMART_MODIFIER_LOCK` | `1` | 是否启用普通 modifier 的双击锁定 |
| `SMART_MODIFIER_MAX_ACTIVE` | `4` | 同时滞留的触发键上限（实现容量；超出时该触发键退化为原生） |

---

## 工作原理

- 触发键就是普通的 modifier / Mod-Tap 键码，模块**不实现 tap/hold 状态机**：普通 modifier 的按下/抬起原生照旧；Mod-Tap 由 QMK core 判定，`record->tap.count == 0` 的抬手表示「hold 半边被用过」。
- mod 以 **weak mods** 应用：`action_exec()` 每次按下会清空 weak mods，所以模块在每个事件都重新 `add_weak_mods`，保证 mod 和消费键在同一份 HID report。weak mods 与 real mods 分开存放，原生松手只会去掉 real mods。
- 每个触发键占一个槽位，保存自己的 mod 掩码、`timeout`、锁定状态与滚动窗口时间戳，因此多个触发键互不干扰、per-key 超时精确。
- 双击判定用 `GET_QUICK_TAP_TERM()`，并且任何其它键按下都会打断双击候选（与原生 tap-hold 的 quick tap 规则一致）。
- `housekeeping_task_smart_modifier()` 检查滞留超时；有键按住时暂停。

---

## 已知限制与注意事项

- **未经实机测试**：本模块由 AI 编写，尚待真实键盘验证。
- 锁定会一直有效，直到再次按下同一个触发键解锁；忘记解锁就会一直保持该 mod（把 `SMART_MODIFIER_LOCK` 设为 `0` 可整体关闭锁定）。
- 本模块接管的键**不会**触发原生 `oneshot_mods_changed_*` 回调，也与原生 oneshot 锁定状态无关。本模块不依赖 One Shot，`NO_ACTION_ONESHOT` 下照常可用。
- weak mods 与其它同样使用 weak mods 的特性（例如 Speculative Hold）可能互相清位；二者一般不会同时启用，若启用请留意。
- 槽位用满（默认 4 个同时滞留）后，新触发键退化为原生行为，不报错。
- **声明与键码必须匹配**：普通 modifier 必须声明为 `SMART_MODIFIER_MOD`，Mod-Tap 必须声明为 `SMART_MODIFIER_MOD_TAP`；不匹配的声明会被忽略。
- 组合键（combo）输出被声明的键码时同样生效（按 keycode 匹配，不依赖矩阵位置），组合键的按住也会暂停计时。
- 需要 QMK `master` 的社区模块 API（`ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0)`）。

### 实机验证清单

上机后建议依次确认：

1. 轻点声明的 modifier（如 `KC_LSFT`）后立刻连打字母，字母都带上修饰键；停手超过 `timeout` 后恢复正常。
2. 按住同一个 modifier 连打，行为与原生完全一致。
3. 快速双击该 modifier 后松手，mod 保持有效；再按一次该键解锁。
4. 未声明的 modifier / Mod-Tap / OSM / LT 行为完全不变。
5. 声明的 Mod-Tap（如 `LGUI_T(KC_I)`）轻点只输出 `i` 且不带 GUI；按住再松手后，后续按键带 GUI，超时后恢复。
6. 分别改动 `SMART_MODIFIER_TIMEOUT`、`SMART_MODIFIER_LOCK`、`SMART_MODIFIER_MAX_ACTIVE` 各验证一次。

---

## 许可

GPL-2.0-or-later。

---

<a name="english"></a>

# smart_modifier (English)

A QMK community module: let plain modifiers (`KC_LSFT`, ...) and Mod-Taps (`LGUI_T(KC_I)`, ...) keep their mod alive for a short while after the key is released.

> **Disclaimer**: This module was generated entirely by AI. It has not been compiled or tested on real hardware, and its code quality is not guaranteed. Review the source and use at your own risk.

---

## The problem it solves

A plain `KC_LSFT` / `KC_RCTL` is active only while it is held, and the mod half of a Mod-Tap is no different: let go and it is gone.

Classic case: hold Shift and type several letters, but release Shift a moment before the last letter is released — the last letter comes out lowercase. Or hold GUI, press one key, and you have to hold GUI again for the next one.

This module lets the mod **linger a little longer**: after the trigger (a plain modifier key, or the hold half of a Mod-Tap) is released, the mod stays active for a short time (1000 ms by default, a rolling window), so a fast burst of keys all gets the modifier.

---

## Behaviour

Only keycodes the callback declares are affected; every other key (other modifiers, other Mod-Taps, OSM, LT, ...) keeps native behaviour.

| Trigger | tap | hold | after release | double tap |
|---|---|---|---|---|
| plain modifier `SMART_MODIFIER_MOD` | native (a modifier tap), and arms the lingering | native real modifier | mod stays active for `timeout` ms | lock the mod (`SMART_MODIFIER_LOCK`, on by default); press the same trigger again to unlock |
| Mod-Tap `SMART_MODIFIER_MOD_TAP` | native base key (e.g. `i`), **no** lingering | native modifier | mod stays active for `timeout` ms | no lock |

- **Rolling window**: every press and release restarts the timers; while any key is held the timers are suspended, so a long keypress is never cut short.
- **Multiple triggers combine**: a lingering Shift plus a lingering Ctrl give the next key Shift+Ctrl; every trigger keeps its own `timeout` and its own lock.
- **The tap/hold decision is left to QMK core**: triggers are ordinary modifier / Mod-Tap keycodes, so `PERMISSIVE_HOLD`, `HOLD_ON_OTHER_KEY_PRESS`, `QUICK_TAP_TERM`, `CHORDAL_HOLD`, `get_tapping_term()` and friends all work natively.
- **The trigger itself is never intercepted**: its press/release goes to native handling exactly as before (so bootmagic mod swaps, `MAGIC_ENABLE`, ... behave identically); the module only adds a lingering copy as weak mods.

### Timeouts

- **Linger timeout** `SMART_MODIFIER_TIMEOUT` (default 1000 ms): how long the mod stays active after the trigger is released; `0` disables lingering (the mod goes away as soon as the trigger is released; the double-tap lock still works).
- Every key can override it per trigger with `cfg->timeout`.

---

## Installation

### 1. Place the module

Put `smart_modifier/` under the `modules/affut1916` directory of your QMK userspace or firmware.

### 2. Enable it in keymap.json

```json
{
    "modules": ["affut1916/smart_modifier"]
}
```

---

## Quick start

```c
#include QMK_KEYBOARD_H
#include "smart_modifier.h"

smart_modifier_mode_t smart_modifier_get(uint16_t keycode, smart_modifier_config_t *cfg) {
    // `cfg` is pre-filled with the defaults; only change what you need.
    switch (keycode) {
        case KC_LSFT:
            return SMART_MODIFIER_MOD;      // 1000 ms by default, double tap locks

        case KC_RCTL:
            cfg->timeout = 500;             // override the default 1000 ms
            return SMART_MODIFIER_MOD;

        case LGUI_T(KC_I):
            return SMART_MODIFIER_MOD_TAP;  // tap = i, hold = GUI
    }
    return SMART_MODIFIER_NONE; // every other key stays native
}
```

Use plain keycodes in your keymap; undeclared ones stay fully native:

```c
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT(
        /* ... */
        KC_LSFT, KC_RCTL, LGUI_T(KC_I), KC_LALT  // KC_LALT undeclared = native
    ),
};
```

See [`example/keymap.c`](example/keymap.c) for a full example.

---

## Configuration

### `smart_modifier_config_t` in the callback

When `smart_modifier_get()` is called, `cfg` is pre-filled with these defaults:

| Field | Default | Description |
|---|---|---|
| `timeout` | `SMART_MODIFIER_TIMEOUT` (1000) | linger timeout in ms; `0` disables lingering (the double-tap lock still works) |
| `lock` | `SMART_MODIFIER_LOCK` (1) | whether this key allows the double-tap lock (plain modifiers only) |

### config.h globals

| Macro | Default | Description |
|---|---|---|
| `SMART_MODIFIER_TIMEOUT` | `1000` | default linger timeout (ms); `0` disables |
| `SMART_MODIFIER_LOCK` | `1` | enable the plain-modifier double-tap lock |
| `SMART_MODIFIER_MAX_ACTIVE` | `4` | max triggers lingering at once (implementation capacity; beyond it a trigger falls back to native) |

---

## How it works

- Triggers are ordinary modifier / Mod-Tap keycodes, and the module **implements no tap/hold state machine**: a plain modifier's press/release stays native, and a Mod-Tap is resolved by QMK core, where a release with `record->tap.count == 0` means the hold half was used.
- The mod is applied as **weak mods**: `action_exec()` clears weak mods on every press, so the module re-applies `add_weak_mods` on every event, keeping the mod and the consuming key in the same HID report. Weak mods live apart from real mods, so a native release only removes the real one.
- Each trigger owns a slot with its own mod mask, `timeout`, lock state and rolling-window timestamp, so triggers never interfere and per-key timeouts are exact.
- The double-tap window uses `GET_QUICK_TAP_TERM()`, and any other key press breaks the candidate, matching a regular tap-hold quick tap.
- `housekeeping_task_smart_modifier()` checks the linger timeouts; it is suspended while any key is held.

---

## Known limitations

- **Not tested on hardware.** This module is AI-authored and awaits validation on a real keyboard.
- A locked mod stays until the same trigger is pressed again; forget to unlock and it lingers forever (`SMART_MODIFIER_LOCK = 0` turns locking off entirely).
- Keys claimed by this module **do not** fire the native `oneshot_mods_changed_*` callbacks and do not interact with the native oneshot locked state. The module does not depend on One Shot, so it works with `NO_ACTION_ONESHOT`.
- Weak mods may be cleared by other features that also use weak mods (e.g. Speculative Hold); they are unlikely to be enabled together, but keep it in mind.
- Once every slot is taken (4 lingering triggers by default), a new trigger falls back to native behaviour without an error.
- **The declared mode must match the keycode**: a plain modifier must be declared as `SMART_MODIFIER_MOD` and a Mod-Tap as `SMART_MODIFIER_MOD_TAP`; a mismatched declaration is ignored.
- A trigger emitted by a combo works too (matching is by keycode, not matrix position), and a held combo suspends the timers.
- Targets QMK `master`'s community-module API (`ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0)`).

### Hardware checklist

Once flashed, confirm in order:

1. Tap a declared modifier (e.g. `KC_LSFT`) and immediately type letters: all of them carry the modifier; after more than `timeout` of idle it behaves natively again.
2. Hold the same modifier and type: behaviour is exactly native.
3. Double tap the modifier and release: the mod stays; press it once more to unlock.
4. Undeclared modifiers / Mod-Taps / OSM / LT are completely unaffected.
5. A declared Mod-Tap (e.g. `LGUI_T(KC_I)`) tapped outputs `i` with no GUI; held and released, the following keys carry GUI until the timeout.
6. Change `SMART_MODIFIER_TIMEOUT`, `SMART_MODIFIER_LOCK` and `SMART_MODIFIER_MAX_ACTIVE` once each and confirm they take effect.

---

## License

GPL-2.0-or-later.
