# AlienFanSuite

> 语言 / Language: [简体中文](#简体中文) · [English](#english)

黑苹果下戴尔 / Alienware 风扇控制套件 · Fan control for Dell / Alienware on macOS

---

## 简体中文

面向**整个戴尔 / Alienware 全系（带外星人控制中心 AWCC）**的黑苹果风扇控制套件：**AlienFan.kext 驱动 + FanControl 图形界面**，按版本配对发布。当前在 Alienware m15 R7 上开发验证。

在 macOS 上通过 ACPI/WMI 的 `WMAX` 命令直接读写 EC，绕过 SMC（12 代 Dell 机器没有 i8k SMM 签名，SMCDellSensors 无法工作）。风扇 boost 叠加在 BIOS 温控曲线之上，100% 对应本机 4800 RPM 上限。

> 发布包里的驱动版本 **2.1.0**，控制软件版本 **1.3.1**。应用设置页会实时检测驱动安装 / 加载 / UserClient 状态，并校验版本是否匹配。

### 功能

- **控制台内曲线引擎主控**：启用/停用开关置于控制台首页，带运行指示灯与状态说明；启停状态跨重启记忆，启动时自动恢复，按钮显示始终与实际运行一致
- 12 点温度/转速曲线（30–100°C → 0–100% boost），可拖动、增删控制点
- 多级档位一键下发（30/50/70/85/100%）；点手动档位会自动停用曲线，避免两者冲突
- 内置静音 / 均衡 / 性能预设，用户预设持久化到 `~/Library/Application Support/AlienFan/presets.json`
- USTT 散热模式切换（0xA0 平衡、0xA2 冷却、0xA3 安静、0xA4 性能……）与 G 模式
- 实时 RPM / 温度读数，菜单栏温度显示与预设快速切换
- 原始 WMAX 命令面板，所有十六进制参数附中文注解
- **设置页**：驱动安装、内核加载、设备就绪、UserClient、版本匹配状态检测；一键重新检测
- **设置页**：开机自启（用户级 LaunchAgent，后台驻留菜单栏）、启动时最小化、状态栏温度来源（最高温 / CPU / 显卡）可切换
- **设置页**：界面语言一键切换 简体中文 / English，偏好本地持久化

### 兼容性

**面向整个戴尔 / Alienware 机型：只要机器带「外星人控制中心」（AWCC, Alienware Command Center），本方案理论上都适用。** 底层走的是 AWCC 的 ACPI-WMI 通用接口，不是某一款机型的私有实现；当前在 Alienware m15 R7 上完成开发与验证。

| 项目 | 说明 |
| --- | --- |
| 机型 | 所有带 AWCC 的戴尔 / Alienware 笔记本（开发验证机：Alienware m15 R7，OpenCore 伪装 `MacBookPro16,2`） |
| 系统 | macOS 11+（x86_64），需要 Command Line Tools 编译 |
| 设备 | ACPI-WMI `PNP0C14`，`_UID = AWCC`，设备名 `AMWW`（各机型此接口通用） |
| 风扇 | 本机 ID `0x32` / `0x33`，MaxRPM 4800；其他机型的 ID / 上限可能不同 |
| 已知限制 | `0x14/0x06` 百分比读数恒返回 `0xFE`（本机），改用 RPM + MaxRPM 计算 |
| 欢迎反馈 | 其他 AWCC 机型的风扇 ID / 传感器映射 / 模式码若有差异，请提 Issue 帮助适配 |

### 下载与安装

从 Releases 下载 `AlienFanSuite-v*.zip`，解压后：

```bash
cd AlienFanSuite-v1.3.1
sudo ./install.sh
```

安装器会：

1. 把 `AlienFan.kext` 装到 `/Library/Extensions`
2. 把 `alienfanctl` 装到 `/usr/local/bin`
3. 把 `FanControl.app` 装到 `/Applications`
4. 尝试 `kmutil load -p` 立即加载；若被系统拦截，按下面步骤批准后重启

#### 驱动批准（必须）

第三方 kext 在 macOS 11+ 默认被拦截。二选一：

- **OpenCore**：把 `AlienFan.kext` 放进 `EFI/OC/Kexts/`，在 `config.plist → Kernel → Add` 增加条目（`BundlePath = AlienFan.kext`，`Enabled = true`），并在 `Kernel → Block` 中通常无需额外操作；重启。
- **系统设置**：出现「系统软件被阻止」提示后，到 `系统设置 → 隐私与安全性 → 安全性` 点「仍要允许」，然后重启。

重启后打开 FanControl，进入 **设置** 页确认：

- `kext 已安装 = 是`
- `内核已加载 = 是`
- `设备就绪 = 是`
- `UserClient 连接 = 是`
- `版本匹配`（App 1.3.1 ↔ 驱动 2.1.0）

如某项为「否」，页面底部会给出对应处理提示。

### 使用

1. 打开 FanControl，在控制台点 **启用曲线**
2. 在曲线编辑页微调 12 个控制点，点 **立即应用**
3. 在预设页保存 / 载入自己的方案
4. 高级页可切散热模式、G 模式，或发原始 WMAX 命令（谨慎）
5. 设置页随时切换语言：**简体中文 / English**

CLI 常用命令：

```bash
alienfanctl info        # 探测设备、systemID、计数
alienfanctl list        # 风扇读数
alienfanctl powers      # 模式表
alienfanctl mode 160    # 0xA0 平衡
alienfanctl boost all 100
alienfanctl boost all 0 # 交还 BIOS 控制
```

### 从源码构建

```bash
# 驱动（需要 Command Line Tools）
cd driver && ./build.sh      # → build/AlienFan.kext, build/alienfanctl

# 应用
cd app && ./build.sh         # → build/FanControl.app

# 打发布包
cd .. && ./scripts/package.sh
# → dist/AlienFanSuite-v<app版本>.zip（驱动 + CLI + App + 安装/卸载脚本）
```

### 卸载

```bash
sudo ./uninstall.sh
```

卸载脚本会卸载 kext、删除 `/Library/Extensions/AlienFan.kext`、`/usr/local/bin/alienfanctl`、`/Applications/FanControl.app`，然后建议重启。曲线预设文件默认保留，如需清理请手动删除 `~/Library/Application Support/AlienFan/`。

### 协议备注

| 命令 | 含义 |
| --- | --- |
| `WMAX(0x14, 0x03, id, 0)` | 读风扇 ID（返回 0x32 / 0x33） |
| `WMAX(0x14, 0x05, 0x32/0x33, 0)` | 读 RPM，a1 必须传 ID 而不是下标 |
| `WMAX(0x14, 0x09, id, 0)` | 读 MaxRPM（4800） |
| `WMAX(0x14, 0x04, id, 0)` | 读温度 |
| `WMAX(0x15, 0x01, 0, mode)` | 写散热模式（USTT 原始码） |
| `WMAX(0x15, 0x02, id, pct)` | 写 boost 百分比，id=0 表示两个风扇 |
| `WMAX(0x25, 0x02, 0, 0)` | 读 G 模式状态 |

详细逆向过程见 [`docs/AWCC-第三方风扇工具逆向分析.md`](docs/AWCC-第三方风扇工具逆向分析.md)。

### 致谢 / 许可

协议实现参考了 [alien-tools](https://github.com/T-Troll/alien-tools)（MIT © 2020 Rik Lain）。本项目同样以 MIT 许可发布，详见 [LICENSE](LICENSE)。

本项目为个人逆向与适配成果，**不提供任何保修**。直接向 EC 写命令存在风险，误用原始 WMAX 可能导致风扇异常。操作前请了解自己的机器，后果自负。

---

## English

A fan control suite for **Hackintosh on the entire Dell / Alienware lineup (any machine with Alienware Command Center, AWCC)**: **AlienFan.kext driver + FanControl GUI**, released as version-locked pairs. Currently developed and verified on an Alienware m15 R7.

On macOS it talks directly to the EC via ACPI/WMI `WMAX` commands, bypassing SMC (12th-gen Dell machines lack the i8k SMM signature, so SMCDellSensors does not work). Fan boost is layered on top of the BIOS thermal curve; 100% maps to this machine's 4800 RPM ceiling.

> The release bundle ships driver **2.1.0** and controller **1.3.1**. The app's Settings page checks driver installation / kernel load / UserClient status in real time and verifies version compatibility.

### Features

- **Curve engine master switch on the console home**: on/off toggle with a live indicator and status text; state persists across reboots and auto-restores on launch — the button always matches what is actually running
- 12-point temperature/RPM curve (30–100°C → 0–100% boost), draggable with add/remove control points
- One-click fixed levels (30/50/70/85/100%); selecting a manual level automatically stops the curve to avoid conflicts
- Built-in Silent / Balanced / Performance presets; user presets persist to `~/Library/Application Support/AlienFan/presets.json`
- USTT thermal mode switching (0xA0 Balanced, 0xA2 Cool, 0xA3 Quiet, 0xA4 Performance, ...) and G Mode
- Live RPM / temperature readouts, menu bar temperature display and quick preset switching
- Raw WMAX command panel, every hex parameter annotated
- **Settings**: driver install, kernel load, device readiness, UserClient, and version-match status checks; one-click re-check
- **Settings**: launch at login (user-level LaunchAgent, menu-bar background), start minimized, menu bar temperature source (hottest / CPU / GPU)
- **Settings**: one-click UI language switch 简体中文 / English, preference persisted locally

### Compatibility

**Works across the entire Dell / Alienware lineup: any laptop with Alienware Command Center (AWCC).** It uses the standard AWCC ACPI-WMI interface, not a model-specific implementation; currently developed and verified on an Alienware m15 R7. Other AWCC models should work — fan IDs, sensor maps and mode codes may differ.

| Item | Detail |
| --- | --- |
| Models | All Dell / Alienware laptops with AWCC (dev & verify machine: Alienware m15 R7, OpenCore SMBIOS `MacBookPro16,2`) |
| OS | macOS 11+ (x86_64); Command Line Tools required to build |
| Device | ACPI-WMI `PNP0C14`, `_UID = AWCC`, device name `AMWW` (common across models) |
| Fans | This machine: IDs `0x32` / `0x33`, MaxRPM 4800; IDs / limits may differ on other models |
| Known limitation | `0x14/0x06` percentage reading always returns `0xFE` on this machine; RPM + MaxRPM are used instead |
| Feedback welcome | If fan IDs / sensor maps / mode codes differ on your AWCC model, please open an Issue |

### Download & Install

Grab `AlienFanSuite-v*.zip` from Releases, unzip and run:

```bash
cd AlienFanSuite-v1.3.1
sudo ./install.sh
```

The installer will:

1. Install `AlienFan.kext` to `/Library/Extensions`
2. Install `alienfanctl` to `/usr/local/bin`
3. Install `FanControl.app` to `/Applications`
4. Try `kmutil load -p` to load immediately; if blocked by the system, approve it below and reboot

#### Approve the driver (required)

Third-party kexts are blocked by default on macOS 11+. Pick one:

- **OpenCore**: put `AlienFan.kext` in `EFI/OC/Kexts/`, add an entry under `config.plist → Kernel → Add` (`BundlePath = AlienFan.kext`, `Enabled = true`); usually no extra `Kernel → Block` action is needed; reboot.
- **System Settings**: after the "system software was blocked" prompt, go to `System Settings → Privacy & Security → Security` and click "Allow Anyway", then reboot.

After reboot, open FanControl and check the **Settings** page:

- `kext installed = Yes`
- `kernel loaded = Yes`
- `device ready = Yes`
- `UserClient connected = Yes`
- `version match` (App 1.3.1 ↔ driver 2.1.0)

If any item reads "No", the bottom of the page shows the corresponding hint.

### Usage

1. Open FanControl and click **Start Curve** on the console home
2. Fine-tune the 12 control points in the curve editor, then click **Apply Now**
3. Save / load your own profiles on the Presets page
4. Advanced page: switch thermal mode, G Mode, or send raw WMAX commands (caution)
5. Switch UI language any time in **Settings → Language** (简体中文 / English)

Common CLI commands:

```bash
alienfanctl info        # probe device, systemID, counts
alienfanctl list        # fan readings
alienfanctl powers      # mode table
alienfanctl mode 160    # 0xA0 Balanced
alienfanctl boost all 100
alienfanctl boost all 0 # hand control back to BIOS
```

### Build from source

```bash
# driver (requires Command Line Tools)
cd driver && ./build.sh      # → build/AlienFan.kext, build/alienfanctl

# app
cd app && ./build.sh         # → build/FanControl.app

# package a release
cd .. && ./scripts/package.sh
# → dist/AlienFanSuite-v<app-version>.zip (driver + CLI + app + install/uninstall scripts)
```

### Uninstall

```bash
sudo ./uninstall.sh
```

The uninstall script unloads the kext and removes `/Library/Extensions/AlienFan.kext`, `/usr/local/bin/alienfanctl` and `/Applications/FanControl.app`, then recommends a reboot. Curve preset files are kept by default; remove `~/Library/Application Support/AlienFan/` manually if you want them gone.

### Protocol notes

| Command | Meaning |
| --- | --- |
| `WMAX(0x14, 0x03, id, 0)` | Read fan ID (returns 0x32 / 0x33) |
| `WMAX(0x14, 0x05, 0x32/0x33, 0)` | Read RPM — a1 must be the ID, not the index |
| `WMAX(0x14, 0x09, id, 0)` | Read MaxRPM (4800) |
| `WMAX(0x14, 0x04, id, 0)` | Read temperature |
| `WMAX(0x15, 0x01, 0, mode)` | Write thermal mode (raw USTT code) |
| `WMAX(0x15, 0x02, id, pct)` | Write boost percentage; id=0 means both fans |
| `WMAX(0x25, 0x02, 0, 0)` | Read G Mode state |

See [`docs/AWCC-第三方风扇工具逆向分析.md`](docs/AWCC-第三方风扇工具逆向分析.md) for the full reverse-engineering write-up.

### Credits / License

The protocol implementation references [alien-tools](https://github.com/T-Troll/alien-tools) (MIT © 2020 Rik Lain). This project is also released under the MIT license — see [LICENSE](LICENSE).

This is a personal reverse-engineering and adaptation effort, provided **with no warranty**. Writing directly to the EC carries risk; misuse of raw WMAX commands may cause fan anomalies. Understand your machine before proceeding — you use it at your own risk.
