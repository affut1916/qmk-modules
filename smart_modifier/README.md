# smart_modifier

> 中文 | [English](#english)

QMK「智能 Modifier」社区模块：一个更聪明的 One Shot Modifier（OSM）。

> **免责声明**：本模块完全由 AI 生成，未经实机编译与测试，代码质量不作任何保证。使用前请自行审阅源码，并自行承担风险。

---

## 它解决什么问题

原生 `OSM(mod)` 是"点一下，下一个键带修饰键"：mod 会一直生效，直到**你应用的那个键抬起**，然后立刻消失。

典型场景：点一下 `OSM(MOD_LSFT)`，然后快速连打几个字母想全大写。第一个字母抬起后 Shift 就没了，后面的字母全变回小写。

本模块让 mod **多留一会儿**：消费键抬起后，mod 继续有效一小段时间（默认 250ms，滚动窗口），所以一串快速按键都能带上修饰键，而你只需要点一次 OSM。

---

## 行为

只对回调里声明为 `SMART_MODIFIER_OSM` 的 OSM 键生效，其它 OSM 键完全保持原生。

| 操作 | 效果 |
|---|---|
| **tap** | 让 mod 进入排队；下一个普通键带上它，并在该键（及之后每个键）抬起后继续有效 `timeout` 毫秒 |
| **双击** | 锁定 mod（`SMART_MODIFIER_LOCK`，默认开）。锁定后忽略超时，再 tap 一次解锁 |
| **再次 tap** | 取消该 mod（非双击时） |
| **按住** | 原生 OSM：按住期间是真实修饰键，松手即撤 |

- **滚动窗口**：每次按下/抬起都重置计时；只要有任意键按住，计时暂停，长按不会被截断。
- **多个触发键可叠加**：先 tap `OSM(MOD_LSFT)` 再 tap `OSM(MOD_LCTL)`，下一个键同时带 Shift+Ctrl。
- **纯修饰键不消费**：`OSM(Shift)` 排队时按住真实 Ctrl 再按 C，得到 Ctrl+Shift+C。
- **tap/hold 判定完全交给 QMK 原生**，所以 `PERMISSIVE_HOLD`、`HOLD_ON_OTHER_KEY_PRESS`、`QUICK_TAP_TERM`、`get_tapping_term()` 等全部原生生效。

### 超时

- **滞留超时** `SMART_MODIFIER_TIMEOUT`（默认 250ms）：消费键抬起后 mod 继续有效的时长。
- **排队超时** `SMART_MODIFIER_QUEUE_TIMEOUT`（默认 0，即不超时）：tap 之后到第一个普通键之间的等待上限。

两者都可在回调里逐键覆盖。

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
        case OSM(MOD_LSFT):
            return SMART_MODIFIER_OSM;

        case OSM(MOD_LCTL):
            cfg->timeout       = 500;  // 覆盖默认 250ms
            cfg->queue_timeout = 1000; // 1 秒内没有键按下就放弃
            return SMART_MODIFIER_OSM;
    }
    return SMART_MODIFIER_NONE; // 其它 OSM 保持原生
}
```

键位图里照样写普通的 `OSM(...)`：

```c
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT(
        /* ... */
        OSM(MOD_LSFT), OSM(MOD_LCTL), /* ... */ OSM(MOD_LALT)
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
| `timeout` | `SMART_MODIFIER_TIMEOUT` (250) | 滞留超时，毫秒；`0` = 不超时 |
| `queue_timeout` | `SMART_MODIFIER_QUEUE_TIMEOUT` (0) | 排队超时，毫秒；`0` = 不超时 |

### config.h 全局宏

| 宏 | 默认 | 说明 |
|---|---|---|
| `SMART_MODIFIER_TIMEOUT` | `250` | 默认滞留超时（毫秒） |
| `SMART_MODIFIER_QUEUE_TIMEOUT` | `0` | 默认排队超时（毫秒）；`0` = 不超时 |
| `SMART_MODIFIER_LOCK` | `1` | 是否启用双击锁定 |

---

## 工作原理

- 触发键就是普通 `OSM(mod)`；`record->tap.count` 区分 tap/hold，模块**只接管 tap 半区**，hold 半区返回 `true` 交给原生 OSM（真实 mod）。
- mod 以 **weak mods** 应用：`action_exec()` 每次按下会清空 weak mods，所以模块在每次按下时重新 `add_weak_mods`，保证 mod 和消费键在同一份 HID report。
- 双击判定用 `GET_QUICK_TAP_TERM()`，与普通 tap-hold 键的 quick tap 一致。
- `housekeeping_task_smart_modifier()` 检查排队超时与滞留超时。

---

## 已知限制与注意事项

- **未经实机测试**：本模块由 AI 编写，尚待真实键盘验证。
- 依赖原生 One Shot Modifier：`NO_ACTION_ONESHOT` 会编译报错。若 One Shot 被关闭（`is_oneshot_enabled()` 为假），触发键会退回原生行为（此时 tap 输出普通 modifier）。
- 本模块接管的键**不会**触发原生 `oneshot_mods_changed_*` 回调，也不受原生 oneshot 锁定状态影响。
- weak mods 与其它同样使用 weak mods 的特性（例如 Speculative Hold）可能互相清位；二者一般不会同时启用，若启用请留意。
- 需要 QMK `master` 的社区模块 API（`ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0)`）。

---

## 许可

GPL-2.0-or-later。

---

<a name="english"></a>

# smart_modifier (English)

A QMK community module: a **smarter One Shot Modifier**.

> **Disclaimer**: This module was generated entirely by AI. It has not been compiled or tested on real hardware, and its code quality is not guaranteed. Review the source and use at your own risk.

---

## The problem it solves

A native `OSM(mod)` holds the mod for one key: it stays active until **the key you apply it to is released**, then it is gone.

Classic case: tap `OSM(MOD_LSFT)` and then type several letters hoping for all caps. The shift disappears the moment the first letter is released, and the rest come out lowercase.

This module lets the mod **linger a little longer**: after the consuming key is released, the mod stays active for a short time (250 ms by default, a rolling window). A fast burst of keys all gets the modifier, while you only tapped the OSM once.

---

## Behaviour

Only OSM keys the callback declares as `SMART_MODIFIER_OSM` are affected; every other OSM key keeps native behaviour.

| Action | Effect |
|---|---|
| **tap** | arm the mod; the next ordinary key gets it, and it stays active for `timeout` ms after that key (and every further key) is released |
| **double tap** | lock the mod (`SMART_MODIFIER_LOCK`, on by default). A locked mod ignores the timeout; tap once more to unlock |
| **tap again** | cancel the mod (when it is not a double tap) |
| **hold** | native OSM: a real modifier while held, released when let go |

- **Rolling window**: every press and release restarts the timer; while any key is held the timer is suspended, so a long keypress is never cut short.
- **Multiple triggers combine**: tap `OSM(MOD_LSFT)` then `OSM(MOD_LCTL)` and the next key gets Shift+Ctrl.
- **Modifier keys do not consume** the mod: with `OSM(Shift)` armed, hold a real Ctrl and press C to get Ctrl+Shift+C.
- **The tap/hold decision is left to QMK core**, so `PERMISSIVE_HOLD`, `HOLD_ON_OTHER_KEY_PRESS`, `QUICK_TAP_TERM`, `get_tapping_term()` and friends all work natively.

### Timeouts

- **Linger timeout** `SMART_MODIFIER_TIMEOUT` (default 250 ms): how long the mod stays active after the consuming key is released.
- **Queue timeout** `SMART_MODIFIER_QUEUE_TIMEOUT` (default 0, i.e. no timeout): how long an armed mod waits for its first ordinary key.

Both can be overridden per key in the callback.

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
        case OSM(MOD_LSFT):
            return SMART_MODIFIER_OSM;

        case OSM(MOD_LCTL):
            cfg->timeout       = 500;  // override the default 250 ms
            cfg->queue_timeout = 1000; // give up if no key follows within 1 s
            return SMART_MODIFIER_OSM;
    }
    return SMART_MODIFIER_NONE; // other OSM keys stay native
}
```

Use plain `OSM(...)` keys in your keymap:

```c
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT(
        /* ... */
        OSM(MOD_LSFT), OSM(MOD_LCTL), /* ... */ OSM(MOD_LALT)
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
| `timeout` | `SMART_MODIFIER_TIMEOUT` (250) | linger timeout in ms; `0` disables |
| `queue_timeout` | `SMART_MODIFIER_QUEUE_TIMEOUT` (0) | queue timeout in ms; `0` disables |

### config.h globals

| Macro | Default | Description |
|---|---|---|
| `SMART_MODIFIER_TIMEOUT` | `250` | default linger timeout (ms) |
| `SMART_MODIFIER_QUEUE_TIMEOUT` | `0` | default queue timeout (ms); `0` disables |
| `SMART_MODIFIER_LOCK` | `1` | enable the double-tap lock |

---

## How it works

- Triggers are ordinary `OSM(mod)` keys; `record->tap.count` tells tap from hold, and the module **claims only the tap half**, returning `true` for the hold half so native OSM handles it (a real mod).
- The mod is applied as **weak mods**: `action_exec()` clears weak mods on every press, so the module re-applies `add_weak_mods` on each press, keeping the mod and the consuming key in the same HID report.
- The double-tap window uses `GET_QUICK_TAP_TERM()`, matching a regular tap-hold key.
- `housekeeping_task_smart_modifier()` checks the queue and linger timeouts.

---

## Known limitations

- **Not tested on hardware.** This module is AI-authored and awaits validation on a real keyboard.
- It relies on native One Shot Modifiers: `NO_ACTION_ONESHOT` is a compile error. If One Shot is turned off (`is_oneshot_enabled()` false), triggers fall back to native behaviour (a tap outputs a plain modifier).
- Keys claimed by this module **do not** fire the native `oneshot_mods_changed_*` callbacks and do not interact with the native oneshot locked state.
- Weak mods may be cleared by other features that also use weak mods (e.g. Speculative Hold); they are unlikely to be enabled together, but keep it in mind.
- Targets QMK `master`'s community-module API (`ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0)`).

---

## License

GPL-2.0-or-later.
