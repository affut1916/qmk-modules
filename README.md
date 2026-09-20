# qmk-modules

个人 QMK 社区模块集合。

> 中文 | [English](#english)

这里存放我自己在用的、可复用的 QMK 社区模块。每个模块都是自包含的，可以独立放入 QMK userspace 或 firmware 中使用。

> **免责声明**：本仓库的代码由 AI 生成，未经实机编译与测试，质量不作保证。使用前请自行审阅源码并自行承担风险。

## 模块列表

| 模块 | 说明 |
|---|---|
| [`smart_layer`](smart_layer/) | 智能层。tap/hold 判定完全复用 QMK 原生 `LT`，点一下即可“粘住”一个层，并在按下非白名单键、超时或清除键时自动关闭。移植自 [urob 的 ZMK 实现](https://github.com/urob/zmk-auto-layer)。 |

## 安装

### 方式一：作为 QMK firmware 的 git submodule

在 QMK firmware 根目录执行：

```bash
cd /path/to/qmk_firmware
mkdir -p modules
git submodule add https://github.com/affut1916/qmk-modules.git modules/affut1916
git submodule update --init --recursive
```

如果使用 SSH：

```bash
git submodule add git@github.com:affut1916/qmk-modules.git modules/affut1916
```

安装后，模块会位于：

```text
qmk_firmware/modules/affut1916/smart_layer
```

### 方式二：作为 QMK userspace 的 git submodule

```bash
cd /path/to/qmk_userspace
mkdir -p modules/affut1916
git submodule add https://github.com/affut1916/qmk-modules.git modules/affut1916
git submodule update --init --recursive
```

### 方式三：手动复制

只复制需要的模块即可：

```bash
mkdir -p <QMK_USERSPACE>/modules/affut1916
cp -r smart_layer <QMK_USERSPACE>/modules/affut1916/
```

或复制到 firmware：

```bash
mkdir -p /path/to/qmk_firmware/modules/affut1916
cp -r smart_layer /path/to/qmk_firmware/modules/affut1916/
```

## 启用模块

在 keymap 的 `keymap.json` 中启用：

```json
{
    "modules": ["affut1916/smart_layer"]
}
```

确保模块目录下存在 `qmk_module.json`，例如：

```text
modules/affut1916/smart_layer/qmk_module.json
```

## 配置

各模块的配置项与键码请查看对应模块的 README：

- [`smart_layer/README.md`](smart_layer/README.md)

## 目录结构

本仓库内：

```text
qmk-modules/
├── README.md
└── smart_layer/
    ├── qmk_module.json   # 模块元数据与键码声明
    ├── config.h          # 空；选项经 #ifndef 暴露在头文件
    ├── smart_layer.h     # 公共 API 与配置项
    ├── smart_layer.c     # 实现
    ├── example/keymap.c  # 用法示例
    └── README.md         # 双语文档
```

作为 submodule 安装到 QMK firmware 后：

```text
qmk_firmware/
└── modules/
    └── affut1916/              # 本仓库
        ├── README.md
        └── smart_layer/
            ├── qmk_module.json
            ├── config.h
            ├── smart_layer.h
            ├── smart_layer.c
            ├── example/keymap.c
            └── README.md
```

## 更新

如果作为 git submodule 安装，在对应仓库根目录执行：

```bash
git submodule update --remote modules/affut1916
```

## 许可

各模块采用 GPL-2.0-or-later。

---

<a name="english"></a>

# qmk-modules (English)

A personal collection of QMK community modules.

> [中文](#qmk-modules) | English

Each module is self-contained and can be dropped into your QMK userspace or firmware.

> **Disclaimer**: The code in this repository is AI-generated and has not been compiled or tested on hardware; quality is not guaranteed. Review the source and use at your own risk.

## Modules

| Module | Description |
|---|---|
| [`smart_layer`](smart_layer/) | Smart Layers. The tap/hold decision is delegated to QMK's native `LT`; tap once to "stick" a layer on, and it auto-closes on a non-whitelisted key, a timeout, or a clear key. Ported from [urob's ZMK work](https://github.com/urob/zmk-auto-layer). |

## Installation

### Option 1: As a git submodule in QMK firmware

From the QMK firmware root:

```bash
cd /path/to/qmk_firmware
mkdir -p modules
git submodule add https://github.com/affut1916/qmk-modules.git modules/affut1916
git submodule update --init --recursive
```

With SSH:

```bash
git submodule add git@github.com:affut1916/qmk-modules.git modules/affut1916
```

After installation, the module is located at:

```text
qmk_firmware/modules/affut1916/smart_layer
```

### Option 2: As a git submodule in QMK userspace

```bash
cd /path/to/qmk_userspace
mkdir -p modules/affut1916
git submodule add https://github.com/affut1916/qmk-modules.git modules/affut1916
git submodule update --init --recursive
```

### Option 3: Manual copy

Copy only the module you need:

```bash
mkdir -p <QMK_USERSPACE>/modules/affut1916
cp -r smart_layer <QMK_USERSPACE>/modules/affut1916/
```

Or into firmware:

```bash
mkdir -p /path/to/qmk_firmware/modules/affut1916
cp -r smart_layer /path/to/qmk_firmware/modules/affut1916/
```

## Enabling a Module

Add it to your keymap's `keymap.json`:

```json
{
    "modules": ["affut1916/smart_layer"]
}
```

Make sure the module contains a `qmk_module.json`, for example:

```text
modules/affut1916/smart_layer/qmk_module.json
```

## Configuration

See each module's README for options and keycodes:

- [`smart_layer/README.md`](smart_layer/README.md)

## Layout

Inside this repository:

```text
qmk-modules/
├── README.md
└── smart_layer/
    ├── qmk_module.json   # module metadata and keycode declarations
    ├── config.h          # empty; options are exposed via #ifndef in the header
    ├── smart_layer.h     # public API and options
    ├── smart_layer.c     # implementation
    ├── example/keymap.c  # usage example
    └── README.md         # bilingual docs
```

After installing as a submodule in QMK firmware:

```text
qmk_firmware/
└── modules/
    └── affut1916/              # this repository
        ├── README.md
        └── smart_layer/
            ├── qmk_module.json
            ├── config.h
            ├── smart_layer.h
            ├── smart_layer.c
            ├── example/keymap.c
            └── README.md
```

## Updating

If installed as a git submodule, run this in the corresponding repository root:

```bash
git submodule update --remote modules/affut1916
```

## License

Each module is GPL-2.0-or-later.