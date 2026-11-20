# AlienFanSuite

面向**整个戴尔 / Alienware 全系（带外星人控制中心 AWCC）**的黑苹果风扇控制套件：**AlienFan.kext 驱动 + FanControl 图形界面**，按版本配对发布。当前在 Alienware m15 R7 上开发验证。

在 macOS 上通过 ACPI/WMI 的 `WMAX` 命令直接读写 EC，绕过 SMC（12 代 Dell 机器没有 i8k SMM 签名，SMCDellSensors 无法工作）。风扇 boost 叠加在 BIOS 温控曲线之上，100% 对应本机 4800 RPM 上限。

> 发布包里的驱动版本 **2.1.0**，控制软件版本 **1.3.0**。应用设置页会实时检测驱动安装 / 加载 / UserClient 状态，并校验版本是否匹配。

## 功能

- **控制台内曲线引擎主控**：启用/停用开关置于控制台首页，带运行指示灯与状态说明；启停状态跨重启记忆，启动时自动恢复，按钮显示始终与实际运行一致
- 12 点温度/转速曲线（30–100°C → 0–100% boost），可拖动、增删控制点
- 多级档位一键下发（30/50/70/85/100%）
- 内置静音 / 均衡 / 性能预设，用户预设持久化到 `~/Library/Application Support/AlienFan/presets.json`
- USTT 散热模式切换（0xA0 平衡、0xA2 冷却、0xA3 安静、0xA4 性能……）与 G 模式
- 实时 RPM / 温度读数，菜单栏温度显示与预设快速切换
- 原始 WMAX 命令面板，所有十六进制参数附中文注解
- **设置页**：驱动安装、内核加载、设备就绪、UserClient、版本匹配状态检测；一键重新检测
- **设置页**：开机自启（用户级 LaunchAgent，后台驻留菜单栏）、启动时最小化、状态栏温度来源（最高温 / CPU / 显卡）可切换
- **设置页**：界面语言一键切换 简体中文 / English，偏好本地持久化

## 兼容性

**面向整个戴尔 / Alienware 机型：只要机器带「外星人控制中心」（AWCC, Alienware Command Center），本方案理论上都适用。** 底层走的是 AWCC 的 ACPI-WMI 通用接口，不是某一款机型的私有实现；当前在 Alienware m15 R7 上完成开发与验证。

| 项目 | 说明 |
| --- | --- |
| 机型 | 所有带 AWCC 的戴尔 / Alienware 笔记本（开发验证机：Alienware m15 R7，OpenCore 伪装 `MacBookPro16,2`） |
| 系统 | macOS 11+（x86_64），需要 Command Line Tools 编译 |
| 设备 | ACPI-WMI `PNP0C14`，`_UID = AWCC`，设备名 `AMWW`（各机型此接口通用） |
| 风扇 | 本机 ID `0x32` / `0x33`，MaxRPM 4800；其他机型的 ID / 上限可能不同 |
| 已知限制 | `0x14/0x06` 百分比读数恒返回 `0xFE`（本机），改用 RPM + MaxRPM 计算 |
| 欢迎反馈 | 其他 AWCC 机型的风扇 ID / 传感器映射 / 模式码若有差异，请提 Issue 帮助适配 |

## 下载与安装

从 Releases 下载 `AlienFanSuite-v*.zip`，解压后：

```bash
cd AlienFanSuite-v1.3.0
sudo ./install.sh
```

安装器会：

1. 把 `AlienFan.kext` 装到 `/Library/Extensions`
2. 把 `alienfanctl` 装到 `/usr/local/bin`
3. 把 `FanControl.app` 装到 `/Applications`
4. 尝试 `kmutil load -p` 立即加载；若被系统拦截，按下面步骤批准后重启

### 驱动批准（必须）

第三方 kext 在 macOS 11+ 默认被拦截。二选一：

- **OpenCore**：把 `AlienFan.kext` 放进 `EFI/OC/Kexts/`，在 `config.plist → Kernel → Add` 增加条目（`BundlePath = AlienFan.kext`，`Enabled = true`），并在 `Kernel → Block` 中通常无需额外操作；重启。
- **系统设置**：出现「系统软件被阻止」提示后，到 `系统设置 → 隐私与安全性 → 安全性` 点「仍要允许」，然后重启。

重启后打开 FanControl，进入 **设置** 页确认：

- `kext 已安装 = 是`
- `内核已加载 = 是`
- `设备就绪 = 是`
- `UserClient 连接 = 是`
- `版本匹配`（App 1.3.0 ↔ 驱动 2.1.0）

如某项为「否」，页面底部会给出对应处理提示。

## 使用

1. 打开 FanControl，顶栏点 **启用曲线**
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

## 从源码构建

```bash
# 驱动（需要 Command Line Tools）
cd driver && ./build.sh      # → build/AlienFan.kext, build/alienfanctl

# 应用
cd app && ./build.sh         # → build/FanControl.app

# 打发布包
cd .. && ./scripts/package.sh
# → dist/AlienFanSuite-v<app版本>.zip（驱动 + CLI + App + 安装/卸载脚本）
```

## 卸载

```bash
sudo ./uninstall.sh
```

卸载脚本会卸载 kext、删除 `/Library/Extensions/AlienFan.kext`、`/usr/local/bin/alienfanctl`、`/Applications/FanControl.app`，然后建议重启。曲线预设文件默认保留，如需清理请手动删除 `~/Library/Application Support/AlienFan/`。

## 协议备注

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

## 致谢 / 许可

协议实现参考了 [alien-tools](https://github.com/T-Troll/alien-tools)（MIT © 2020 Rik Lain）。本项目同样以 MIT 许可发布，详见 [LICENSE](LICENSE)。

本项目为个人逆向与适配成果，**不提供任何保修**。直接向 EC 写命令存在风险，误用原始 WMAX 可能导致风扇异常。操作前请了解自己的机器，后果自负。

---

## English Quick Start

- Bundle: `AlienFan.kext` 2.1.0 + `FanControl.app` 1.3.0, version-locked.
- **Compatibility: works across Dell / Alienware laptops — any machine with Alienware Command Center (AWCC).** It uses the standard AWCC ACPI-WMI interface, not a model-specific implementation. Developed and verified on an Alienware m15 R7; other AWCC models should work — fan IDs / sensor maps / mode codes may differ, feedback welcome.
- Install: unzip and `sudo ./install.sh`, then approve the kext in System Settings (or add it to OpenCore `Kernel → Add`) and reboot.
- Open FanControl → **Settings** to verify driver installation, kernel load, device readiness, UserClient connection and version compatibility.
- Switch UI language any time in **Settings → Language** (简体中文 / English).
- In **Settings → Startup & Menu Bar** you can enable launch-at-login (background, menu-bar only), start-minimized, and choose the menu bar temperature source (hottest / CPU / GPU).
- Build from source: `driver/build.sh`, `app/build.sh`, `scripts/package.sh`.
- MIT licensed. Protocol reference: alien-tools (MIT, Rik Lain). Use at your own risk.
