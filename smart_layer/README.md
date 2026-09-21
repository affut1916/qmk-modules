# smart_layer

> 中文 | [English](#english)

QMK 智能层（Smart Layer）社区模块。灵感与功能移植自 urob 的 ZMK 实现：

- [urob/zmk-config](https://github.com/urob/zmk-config) — Numword、Smart-Mouse、`smart_num` 的用法
- [urob/zmk-auto-layer](https://github.com/urob/zmk-auto-layer) — 核心的 `auto-layer` behavior

> **免责声明**：本模块完全由 AI 生成，未经实机编译与测试，代码质量不作任何保证。使用前请自行审阅源码，并自行承担风险。

---

## 目录

- [它解决什么问题](#它解决什么问题)
- [核心设计](#核心设计)
- [安装](#安装)
- [快速上手](#快速上手)
- [SL 与 SLT 详解](#sl-与-slt-详解)
- [作为 Combo 的输出](#作为-combo-的输出)
- [层如何自动关闭](#层如何自动关闭)
- [配置项](#配置项)
- [注意事项与行为细则](#注意事项与行为细则)
- [工作原理](#工作原理)
- [已知限制](#已知限制)

---

## 它解决什么问题

传统 `LT(layer, kc)` 的 hold 是 `MO(layer)`：**按住期间**层才有效，松手即关。你必须持续按住它才能用层里的键。

智能层要的是另一种手感：**点一下就把层“粘”住**，之后可以松手自由输入，直到：

- 你按了一个“不属于这个层”的键（比如数字层里按了字母），或
- 一段时间没有操作（可配置的超时），或
- 你显式按下清除键。

这正是 Jonas Hietala 的 [Numword](https://www.jonashietala.se/blog/2021/06/03/the-t-34-keyboard-layout/) 与 urob 的 `auto-layer` 所做的事：输入数字时数字层一直开着，一旦按空格/字母就自动关闭。

---

## 核心设计

本模块**不自己实现 tap/hold 判定**。它复用 QMK 原生的 `LT`：

1. 你像平常一样写 `LT(layer, kc)`；
2. QMK 的 `action_tapping.c` 完成 tap/hold 判定；
3. 模块在 `process_record_smart_layer()` 里用 `record->tap.count` 判断结果，**只改写它需要的那一半**，另一半原样交给 QMK。

好处是：`PERMISSIVE_HOLD`、`HOLD_ON_OTHER_KEY_PRESS`、`CHORDAL_HOLD`、`FLOW_TAP_TERM`、`SPECULATIVE_HOLD`、`get_tapping_term()`、`QUICK_TAP_TERM` 等所有 tap-hold 特性**零配置、原生生效**，与你的 HRM 设置完全一致，无需任何额外开关。

模块只负责一件事：**决定粘滞层何时关闭**。

---

## 安装

### 1. 放置模块

把 `smart_layer/` 放到 QMK userspace 或 firmware 的 `modules/affut1916` 目录下：



### 2. 在 keymap.json 中启用

如果 keymap 目录下还没有 `keymap.json`，新建一个：

```json
{
    "modules": ["affut1916/smart_layer"]
}
```

如果已有 `keymap.json`，把 `"smart_layer"` 加入 `modules` 数组即可。

模块会提供一个键码 `SMART_LAYER_OFF`（别名 `SL_OFF`）。

---

## 快速上手

### 1. 在 keymap.c 里声明触发键

触发键就是普通的 `LT`，用 `#define` 起个可读的名字：

```c
#include QMK_KEYBOARD_H
#include "smart_layer.h"

enum layers { BASE, NAV, NUM };

// SL : tap = 粘滞层, hold = 原生 MO
#define SL_NUM  LT(NUM, KC_NO)
// SLT: tap = 普通键, hold = 粘滞层
#define SLT_TAB LT(NAV, KC_TAB)
```

### 2. 实现配置回调

模块会调用 `smart_layer_get()` 询问每个键码是不是智能层触发键：

```c
smart_layer_mode_t smart_layer_get(uint16_t keycode, smart_layer_config_t *cfg) {
    // 进入回调时 cfg 已经填好默认值，只改你需要改的。
    switch (keycode) {
        case SL_NUM:
            return SMART_LAYER_SL;

        case SLT_TAB:
            cfg->timeout = 500; // 覆盖默认的 3000ms
            return SMART_LAYER_SLT;
    }
    return SMART_LAYER_NONE;
}
```

### 3. 放进键位图

```c
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [BASE] = LAYOUT(
        /* ... */
        SL_NUM, SLT_TAB, /* ... */ SL_OFF
    ),
    /* ... */
};
```

建议留一个 `SL_OFF` 键，作为万能的“关闭所有智能层”开关。

完整示例见 [`example/keymap.c`](example/keymap.c)。

---

## SL 与 SLT 详解

两种触发键的 tap / hold **刚好互补**：

| 键码 | 写法 | tap | hold |
|---|---|---|---|
| **SL** | `LT(layer, KC_NO)` | 激活粘滞层 | 原生 `MO(layer)` |
| **SLT** | `LT(layer, kc)` | 原生 `kc` | 激活粘滞层 |

### SL — tap 开层，hold 当普通 MO

`SL_NUM = LT(NUM, KC_NO)`

- **单击（tap）**：激活（粘滞）Num 层。松手后层保持开启。
- **窗口内双击**：**锁定**该层。锁定后无视超时、无视白名单，只能再次单击解锁（解锁会顺带关层）或按 `SL_OFF`。双击锁定由全局开关 `SMART_LAYER_SL_DOUBLE_TAP_LOCK`（默认开）控制。
- **再次单击**（双击窗口之外）：关闭该层。
- **按住（hold）**：就是原生 `MO(NUM)`，按住期间开、松手即关。

### SLT — tap 输字符，hold 开层

`SLT_TAB = LT(NAV, KC_TAB)`

- **单击（tap）**：输出 `KC_TAB`，与普通 LT 完全一样（包含原生 quick-tap 连击：快速点按再按住会重复 Tab）。
- **按住（hold）**：激活（粘滞）NAV 层，松手后层保持开启。
- **再次按住**：关闭该层。

也就是说，**SL 把“粘滞”放在 tap 上，SLT 把“粘滞”放在 hold 上**，你可以按手感自由选择。

### 作为 Combo 的输出

触发键也可以放在 combo 的输出里：QMK 会把 combo 的 `keycode` 交给本模块，模块以 **keycode**（而非矩阵位置）识别触发键，因此**同一个 `LT(6, KC_NO)` 无论直接放键位还是由 combo 输出，都共用同一个粘滞层**，开启、关闭、auto-exit、timeout、白名单行为一致。

```c
#define SL_NUM LT(NUM, KC_NO)

// 两个键同时按下输出 SL_NUM
const uint16_t PROGMEM combo_num[] = {KC_J, KC_K, COMBO_END};
combo_t key_combos[] = {
    COMBO(combo_num, SL_NUM),
};
```

- 直接键与 combo 输出的同一 LT 会合并为同一个粘滞层；同一个 LT 出现在多个位置时同理。
- **combo 的组成键必须在目标层上保持透明**（`KC_TRANSPARENT`）。否则层开启后这些位置会解析成别的键、combo 不再匹配。例如上面的 `KC_J/K` 要在 `NUM` 层写成 `_______`。
- combo 是 `COMBO_EVENT`，**不参与 auto-exit**（不会关闭其它粘滞层），但会正常计入“有键被按住”，按住时暂停 timeout。
- 若在一次触发后、`QUICK_TAP_TERM` 窗口内再次触发同一个 combo，会被判定为双击而**锁定**该层。可用 `SMART_LAYER_SL_DOUBLE_TAP_LOCK 0` 关闭锁定。

---

## 层如何自动关闭

一个粘滞层会在以下任一情况关闭：

1. **按下了一个“不属于该层”的普通键**。
   默认白名单规则：该键在你**目标层上对应位置有定义**（即不是 `KC_TRANSPARENT`），就算“属于该层”，层继续开着；否则关闭。
   例如 Num 层在 `BSPC` 的位置定义了 Backspace，那按 `BSPC` 不会关层；按一个 Num 层上没有的字母就会关层。

2. **空闲超时到达**（默认 3000ms，可逐键覆盖；设为 `0` 表示禁用）。
   只要**当前有任意键被按住**，超时暂停，不会被截断。

3. **按下 `SL_OFF`**：清除所有智能层（若启用了 Caps Word，一并关闭）。

> 注意：combo、宏等**非物理按键事件**不会关闭智能层。
> 但一个输出 LT 的 combo 仍然可以**开启/关闭**它自己的粘滞层（见下节）。

---

## 配置项

### 回调中的 `smart_layer_config_t`

进入 `smart_layer_get()` 时，`cfg` 已被填入下列默认值，按需覆盖即可：

| 字段 | 默认 | 说明 |
|---|---|---|
| `timeout` | `SMART_LAYER_DEFAULT_TIMEOUT` (3000) | 空闲超时，毫秒；`0` 禁用 |
| `swallow_exit` | `SMART_LAYER_SWALLOW_EXIT` (0) | 触发自动退出的那个键是否被吞掉（不发送） |
| `continue_list` | `NULL` | 额外白名单键码数组 |
| `continue_list_size` | `0` | 上面数组的长度 |

**自定义白名单**示例（数字层在退格、删除、小数点、逗号上延续）：

```c
static const uint16_t num_continue[] = {KC_BSPC, KC_DEL, KC_DOT, KC_COMM};

smart_layer_mode_t smart_layer_get(uint16_t keycode, smart_layer_config_t *cfg) {
    if (keycode == SL_NUM) {
        cfg->continue_list      = num_continue;
        cfg->continue_list_size = sizeof(num_continue) / sizeof(num_continue[0]);
        return SMART_LAYER_SL;
    }
    return SMART_LAYER_NONE;
}
```

**吞掉退出键**示例（让那个键只用来退出层，不产生输入）：

```c
case SL_NUM:
    cfg->swallow_exit = true;
    return SMART_LAYER_SL;
```

### config.h 全局宏

可在你的 `config.h` 中覆盖：

| 宏 | 默认 | 说明 |
|---|---|---|
| `SMART_LAYER_MAX_ACTIVE` | `8` | 同时追踪的粘滞层数量上限（与 `MAX_LAYER` 无关） |
| `SMART_LAYER_DEFAULT_TIMEOUT` | `3000` | 默认空闲超时（毫秒） |
| `SMART_LAYER_SWALLOW_EXIT` | `0` | 默认是否吞掉退出触发键 |
| `SMART_LAYER_SL_DOUBLE_TAP_LOCK` | `1` | 是否启用 SL 双击锁定 |

---

## 注意事项与行为细则

- **触发键在它激活的层上必须保持透明（`KC_TRANSPARENT`）**。否则层开启后该位置会解析成别的键码，导致你无法再次 tap/hold 它来关闭。这是最重要的一条。
- **智能层互相独立**：激活一个智能层**不会**关闭另一个。每个层各自被白名单、超时或 `SL_OFF` 关闭。这与 urob 原版 `auto-layer` 的语义一致。
- **三种“层激活动作”不算普通按键**，所以不会关闭其它智能层：SL tap、SL hold、SLT hold。
- **SLT 的 tap 算普通按键**（它真的发送了一个字符），因此**会**参与白名单、可能关闭其它智能层。这是符合直觉的行为。
- 因为底层就是原生 `LT`，与 HRM（`CHORDAL_HOLD` 等）完全兼容，无需额外配置。
- 触发键按 **keycode** 追踪，因此层开启后仍能识别同一个 LT，直接键与 combo 输出也共用同一个粘滞层。

---

## 工作原理

- `process_record_smart_layer()`：
  - 用 `record->tap.count` 区分 tap / hold；
  - 对触发键，处理它负责的那一半（SL 的 tap、或 SLT 的 hold）并 `return false`；
  - 另一半返回 `true`，放行到 QMK 原生处理（SL 的 hold → `MO`；SLT 的 tap → 普通键）。
- `housekeeping_task_smart_layer()`：检查空闲超时。
- `layer_state_set_smart_layer()`：对仍处于粘滞状态的层重新置位，防止被外部 `MO`/`MT` 的松手误关。
- 触发键以 `keycode` 记录，保证层开启后仍能识别，并让物理键与 combo 输出共享同一粘滞层。

---

## 已知限制

- **未经实机测试**。本模块由 AI 编写，尚待真实键盘验证；请在合入日常使用前充分测试。
- 行为基于 QMK `master` 的社区模块 API（`ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 1, 0)`），较旧的 QMK 版本可能不兼容。
- split 键盘、`swallow_exit` 与 combo 的组合等边界场景未经验证。
- combo 输出的触发键若在 `QUICK_TAP_TERM` 窗口内被快速二次触发，会按双击锁定处理。

---

## 许可

GPL-2.0-or-later。

---

<a name="english"></a>

# smart_layer (English)

A QMK community module for **Smart Layers**. Inspired by and ported from urob's ZMK work:

- [urob/zmk-config](https://github.com/urob/zmk-config) — the Numword / Smart-Mouse / `smart_num` usage
- [urob/zmk-auto-layer](https://github.com/urob/zmk-auto-layer) — the core `auto-layer` behavior

> **Disclaimer**: This module was generated entirely by AI. It has not been compiled or tested on real hardware, and its code quality is not guaranteed. Review the source and use at your own risk.

---

## Table of contents

- [The problem it solves](#the-problem-it-solves)
- [Core design](#core-design)
- [Installation](#installation)
- [Quick start](#quick-start)
- [SL and SLT in detail](#sl-and-slt-in-detail)
- [As a combo output](#as-a-combo-output)
- [How a layer closes](#how-a-layer-closes)
- [Configuration](#configuration)
- [Notes and details](#notes-and-details)
- [How it works](#how-it-works)
- [Known limitations](#known-limitations)

---

## The problem it solves

A normal `LT(layer, kc)` holds `MO(layer)`: the layer is only active **while you hold the key**. You have to keep it pressed to use the layer.

A smart layer gives a different feel: **tap once to "stick" the layer on**, then type freely. It turns off when:

- you press a key that does not "belong" to the layer, or
- some idle time passes (configurable timeout), or
- you press an explicit clear key.

This is what Jonas Hietala's [Numword](https://www.jonashietala.se/blog/2021/06/03/the-t-34-keyboard-layout/) and urob's `auto-layer` do: the number layer stays on while you type digits and auto-off on space or a letter.

---

## Core design

This module does **not** implement tap/hold itself. It reuses QMK's native `LT`:

1. you write `LT(layer, kc)` as usual;
2. QMK's `action_tapping.c` settles the tap/hold decision;
3. the module reads `record->tap.count` in `process_record_smart_layer()` and **only rewrites the half it owns**, leaving the other half to QMK.

As a result, `PERMISSIVE_HOLD`, `HOLD_ON_OTHER_KEY_PRESS`, `CHORDAL_HOLD`, `FLOW_TAP_TERM`, `SPECULATIVE_HOLD`, `get_tapping_term()`, `QUICK_TAP_TERM` and every other tap-hold feature work **natively with zero configuration**, exactly matching your HRM setup.

The module only decides **when a sticky layer closes**.

---

## Installation

### 1. Place the module

Put `smart_layer/` under the `modules/affut1916` directory of your QMK userspace or firmware:



### 2. Enable it in keymap.json

Create a `keymap.json` if you don't have one:

```json
{
    "modules": ["affut1916/smart_layer"]
}
```

Otherwise add `"smart_layer"` to the existing `modules` array.

The module provides one keycode, `SMART_LAYER_OFF` (alias `SL_OFF`).

---

## Quick start

### 1. Declare triggers in keymap.c

Triggers are ordinary `LT` keys, named with `#define`:

```c
#include QMK_KEYBOARD_H
#include "smart_layer.h"

enum layers { BASE, NAV, NUM };

// SL : tap = sticky layer, hold = native MO
#define SL_NUM  LT(NUM, KC_NO)
// SLT: tap = normal key,   hold = sticky layer
#define SLT_TAB LT(NAV, KC_TAB)
```

### 2. Implement the callback

The module asks `smart_layer_get()` whether a keycode is a trigger:

```c
smart_layer_mode_t smart_layer_get(uint16_t keycode, smart_layer_config_t *cfg) {
    // `cfg` is pre-filled with the defaults; only change what you need.
    switch (keycode) {
        case SL_NUM:
            return SMART_LAYER_SL;

        case SLT_TAB:
            cfg->timeout = 500; // override the default 3000 ms
            return SMART_LAYER_SLT;
    }
    return SMART_LAYER_NONE;
}
```

### 3. Put them in your keymap

```c
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [BASE] = LAYOUT(
        /* ... */
        SL_NUM, SLT_TAB, /* ... */ SL_OFF
    ),
    /* ... */
};
```

Keeping an `SL_OFF` key around is recommended as a universal "clear all smart layers" switch.

See [`example/keymap.c`](example/keymap.c) for a full example.

---

## SL and SLT in detail

The tap/hold meanings are **complementary**:

| Keycode | Form | tap | hold |
|---|---|---|---|
| **SL** | `LT(layer, KC_NO)` | activate sticky layer | native `MO(layer)` |
| **SLT** | `LT(layer, kc)` | native `kc` | activate sticky layer |

### SL — tap turns the layer on, hold is a plain MO

`SL_NUM = LT(NUM, KC_NO)`

- **tap**: activate (stick) the Num layer. It stays on after release.
- **double-tap within the window**: **lock** the layer. A locked layer ignores both the timeout and the whitelist; tap again to unlock (which turns it off) or press `SL_OFF`. Controlled by `SMART_LAYER_SL_DOUBLE_TAP_LOCK` (default on).
- **tap again** (outside the double-tap window): turn the layer off.
- **hold**: native `MO(NUM)` — on while held, off on release.

### SLT — tap sends a character, hold turns the layer on

`SLT_TAB = LT(NAV, KC_TAB)`

- **tap**: send `KC_TAB`, exactly like a normal LT (including native quick-tap repeat).
- **hold**: activate (stick) the NAV layer; it stays on after release.
- **hold again**: turn the layer off.

In short: **SL puts "sticky" on tap, SLT puts "sticky" on hold** — pick whichever feels right.

### As a combo output

A trigger may also be the output of a combo: QMK hands the combo's `keycode` to this module, which identifies triggers by **keycode** (not matrix position). So the **same `LT(6, KC_NO)`, whether placed directly or emitted by a combo, shares one sticky layer** with identical on/off, auto-exit, timeout, and whitelist behavior.

```c
#define SL_NUM LT(NUM, KC_NO)

// Two keys pressed together emit SL_NUM
const uint16_t PROGMEM combo_num[] = {KC_J, KC_K, COMBO_END};
combo_t key_combos[] = {
    COMBO(combo_num, SL_NUM),
};
```

- A directly-placed key and a combo output of the same LT merge into one sticky layer; an LT appearing in multiple places likewise.
- **The combo's member keys must stay transparent** (`KC_TRANSPARENT`) on the target layer. Otherwise, once the layer is on, those positions resolve to other keycodes and the combo no longer matches. In the example above, `KC_J`/`KC_K` must be `_______` on the `NUM` layer.
- A combo is a `COMBO_EVENT`, so it **never participates in auto-exit** (it will not close other sticky layers), but it does count as a held key, suspending the timeout while held.
- If the same combo is triggered again within `QUICK_TAP_TERM` of a trigger, it is treated as a double tap and **locks** the layer. Disable locking with `SMART_LAYER_SL_DOUBLE_TAP_LOCK 0`.

---

## How a layer closes

A sticky layer closes when any of these happens:

1. **An ordinary key that does not belong to the layer is pressed.**
   Default whitelist: if the key has a definition (not `KC_TRANSPARENT`) at that position on the target layer, it belongs to the layer and the layer stays on; otherwise it closes.
   E.g. if the Num layer defines Backspace at the `BSPC` position, pressing `BSPC` keeps the layer on; pressing a letter not on the Num layer closes it.

2. **The idle timeout elapses** (default 3000 ms, per-key overridable; `0` disables it).
   While **any key is held**, the timeout is suspended, so a long keypress never cuts the layer short.

3. **`SL_OFF` is pressed**: clears every smart layer (also turns Caps Word off, if enabled).

> Note: non-physical events such as combos and macros never close a smart layer.
> A combo that *emits* an LT can still turn its own sticky layer on/off (see above).

---

## Configuration

### `smart_layer_config_t` in the callback

When `smart_layer_get()` is called, `cfg` is pre-filled with these defaults:

| Field | Default | Description |
|---|---|---|
| `timeout` | `SMART_LAYER_DEFAULT_TIMEOUT` (3000) | idle timeout in ms; `0` disables |
| `swallow_exit` | `SMART_LAYER_SWALLOW_EXIT` (0) | whether the auto-exit key is swallowed (not sent) |
| `continue_list` | `NULL` | extra whitelist keycodes |
| `continue_list_size` | `0` | length of the array above |

**Custom whitelist** example (keep the number layer on for backspace/delete/dot/comma):

```c
static const uint16_t num_continue[] = {KC_BSPC, KC_DEL, KC_DOT, KC_COMM};

smart_layer_mode_t smart_layer_get(uint16_t keycode, smart_layer_config_t *cfg) {
    if (keycode == SL_NUM) {
        cfg->continue_list      = num_continue;
        cfg->continue_list_size = sizeof(num_continue) / sizeof(num_continue[0]);
        return SMART_LAYER_SL;
    }
    return SMART_LAYER_NONE;
}
```

**Swallow the exit key** example (the key only exits the layer, without typing):

```c
case SL_NUM:
    cfg->swallow_exit = true;
    return SMART_LAYER_SL;
```

### config.h globals

Override in your `config.h`:

| Macro | Default | Description |
|---|---|---|
| `SMART_LAYER_MAX_ACTIVE` | `8` | max tracked sticky layers (unrelated to `MAX_LAYER`) |
| `SMART_LAYER_DEFAULT_TIMEOUT` | `3000` | default idle timeout (ms) |
| `SMART_LAYER_SWALLOW_EXIT` | `0` | default for swallowing the exit key |
| `SMART_LAYER_SL_DOUBLE_TAP_LOCK` | `1` | enable SL double-tap lock |

---

## Notes and details

- **A trigger must be transparent (`KC_TRANSPARENT`) on the layer it activates.** Otherwise, once the layer is on, that position resolves to a different keycode and you cannot tap/hold it again to close. This is the most important rule.
- **Smart layers are independent**: activating one does **not** close another. Each closes on its own via the whitelist, timeout, or `SL_OFF`. This matches urob's original `auto-layer` semantics.
- **The three layer-activation halves are not ordinary presses**, so they do not close other smart layers: SL tap, SL hold, SLT hold.
- **An SLT tap is an ordinary key** (it really sends a character), so it **does** participate in the whitelist and may close other smart layers. That is the intuitive behavior.
- Because it builds on native `LT`, it is fully compatible with HRM (`CHORDAL_HOLD`, etc.) with no extra configuration.
- Triggers are tracked by **keycode**, so the same LT stays identifiable once the layer is on, and a direct key and a combo output share one sticky layer.

---

## How it works

- `process_record_smart_layer()`:
  - uses `record->tap.count` to tell tap from hold;
  - for a trigger, handles the half it owns (SL tap, or SLT hold) and returns `false`;
  - returns `true` for the other half, letting QMK handle it natively (SL hold → `MO`; SLT tap → normal key).
- `housekeeping_task_smart_layer()`: checks the idle timeout.
- `layer_state_set_smart_layer()`: re-asserts still-sticky layers so an external `MO`/`MT` release cannot mistakenly turn them off.
- Triggers are stored as a `keycode` so they remain identifiable once the layer is on, and a physical key and a combo output share one sticky layer.

---

## Known limitations

- **Not tested on hardware.** This module is AI-authored and awaits validation on a real keyboard; test thoroughly before daily use.
- Targets QMK `master`'s community-module API (`ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 1, 0)`); older QMK may be incompatible.
- Edge cases such as split keyboards and `swallow_exit` combined with combos are unverified.
- A combo output trigger rapidly re-triggered within `QUICK_TAP_TERM` is treated as a double tap and locks the layer.

---

## License

GPL-2.0-or-later.
