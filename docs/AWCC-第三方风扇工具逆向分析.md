# 第三方「外星人控制中心」风扇工具 —— 逆向分析报告

分析对象：T-Troll/alien-tools（原 `alienfx-tools`，因戴尔投诉改名），release 10.0.0.0
分析方式：REA PE 结构层 + 导入表/字符串静态提取 + 上游源码交叉验证
分析时间：2026-11-18

---

## 0. 一句话结论

这是目前最成熟的 Alienware/Dell 风扇控制第三方实现，**只能在 Windows 上跑**：

- **新版（9.x）** 走 Windows WMI（`root\WMI:AWCCWmiMethodFunction`），通过 COM 调用；
- **旧版（v1）** 自带一个未签名内核驱动 `HwAcc.sys`，直接下发 ACPI 方法调用。

但它反过来证明了一件重要的事：**你这台机器的风扇控制接口是存在的**——ACPI-WMI 设备 `AWMI`、`AMWW` 等 10 个 `PNP0C14` 设备全都在。之前 SMCDellSensors 走的是 SMM 路线（12 代已被封），这条路走不通；而 WMI/ACPI 这条路线在固件里是活的。

---

## 1. 分析对象

| 项 | 值 |
|---|---|
| 仓库 | `T-Troll/alien-tools`（原名 alienfx-tools，重定向自 repo id 276839917） |
| Release | 10.0.0.0「Project renamed after Dell strike」 |
| 主程序 | `alienfan-cli.exe` 232,960 B，PE32+ x86-64 |
| SHA-256 | `43370a0b756775f3b931ce5ff5d6d34df6f8244668556940b292520167fa918c` |
| 版本串 | `AlienFan-CLI v9.6.0.1` |
| 附带 | `alienfan-gui.exe`、`alien-mon.exe`、`LightFX.dll`、v1 版 4 个 exe |
| 授权 | 开源（源码完整可读，C++ / Win32） |

本地存放：`/Users/dongdong/WorkBuddy/2026-11-18-09-45-29/rea-awcc/`

---

## 2. REA 分析结果

### 2.1 PE 结构

```
machine=8664 (x86-64)  PE32+  sections=7
  .text    va=0x1000   vsize=0x26350
  .rdata   va=0x28000  vsize=0xf17a     ← 方法名/命令行文本都在这
  .data    va=0x38000  vsize=0x1884
  .pdata / .fptable / .rsrc / .reloc
资源目录：仅 3 条 RT_MANIFEST(24)，语言 1033，无图标/清单外载荷
```

（注：本机未安装 Ghidra（缺 Java）、Hopper、IDA，Kuna 尚不支持 PE，所以 REA 只跑到 PE 结构层，没有做到函数级反编译。下面用导入表 + 字符串 + 上游源码补足。）

### 2.2 导入表 —— 决定了它「怎么碰硬件」

**alienfan-cli.exe（新版）**

| DLL | 关键函数 | 含义 |
|---|---|---|
| `ole32.dll` | `CoCreateInstance`、`CoInitializeSecurity`、`CoInitializeEx` | **走 COM → WMI**（`IWbemLocator` 在运行时取 CLSID） |
| `ADVAPI32.dll` | `RegCreateKeyExA`、`RegSetValueExA`、`RegDeleteTreeA` | 写 `SOFTWARE\Alienfan` 配置 |
| `POWRPROF.dll` | `PowerSetActiveScheme`、`PowerWriteACValueIndex` | 顺带切 Windows 电源方案 |
| `KERNEL32.dll` | 无 `DeviceIoControl` | **确认：新版完全不碰驱动/IOCTL** |

**alienfan-gui.exe** 另外还导入：`WININET.dll`（`InternetOpenUrlA` 等，检查更新用）、`ADVAPI32` 的 `OpenSCManagerA/ControlService/StartServiceA`（服务控制，配合 `DisableAWCC` 关掉官方 AWCC 服务避免抢控制权）。

**v1\alienfan-cli.exe（旧版）** 多出：

| DLL | 关键函数 | 含义 |
|---|---|---|
| `KERNEL32.dll` | `DeviceIoControl`、`LoadLibraryA("kdl.dll")` | 下发 IOCTL 给自带驱动 |
| `SETUPAPI.dll` | `SetupDiGetDeviceInstanceIdA` 等 | **枚举 ACPI 设备实例**（找 `ACPI\AMWW` 之类） |

### 2.3 字符串证据

新版（`alienfan-cli.exe`）：

```
ROOT\WMI
AWCCWmiMethodFunction
Thermal_Information
Thermal_Control
GetFanSensors
GetThermalInfo2
SetThermalControl2
setfans=<fan1>[,<fanN>][,mode]   Set fans boost level (0..100 - in percent)
setover=...                       Set overboost for selected fan
Unlock fan controls
```

旧版（`v1\alienfan-cli.exe`）：

```
\_SB.AMW1.WMAX      \_SB.AMW3.WMAX
\_SB.AMWW.WMAX      \_SB.WMI2.WMAX
\_SB.AMW1.ICPC      \_SB.AMW1.SETC
\_SB.AMW1.SETB
\HwAcc.sys          kdl.dll      ACPI_HAL
DeviceIoControl
```

这两组字符串把两条技术路线讲得很清楚：**v1 = 直接求值 ACPI 方法；v2 = 调 WMI 类**。

---

## 3. 两代控制通道对比

| | v1（旧，Direct ACPI） | v2（新，WMI） |
|---|---|---|
| 入口 | `IOCTL_ACPI_EVAL_METHOD_EX` → `\\.\ACPI` | `root\WMI` → `AWCCWmiMethodFunction` |
| 目标 | `\_SB.AMW1/AMW3/AMWW/WMI2` 下的 `WMAX`/`ICPC`/`SETC`/`SETB` | 方法 `Thermal_Information`/`Thermal_Control`/`GetFanSensors`/`GetThermalInfo2`/`SetThermalControl2` |
| 依赖 | 自带未签名内核驱动 `HwAcc.sys` | Windows WMI 服务 + 固件 WMI-ACPI 映射 |
| 权限 | 必须关「易受攻击驱动阻止列表」，杀软会报 | 管理员（访问 WMI） |
| 适用 | 部分 G 系列、AMD 机型、需要更高转速的场合 | 主流 Alienware / 2021+ 机型 |

上游 README 对 v1 的要求写得很直白：改注册表 `VulnerableDriverBlocklistEnable=0` 才肯加载驱动——这也是杀软报毒的原因。

---

## 4. WMI 协议还原

方法 ID（来自 SDK 源码与 `tcc-g15` 的 `WMI-AWCC-doc.md` 一致）：

| WmiMethodId | 方法 | 作用 |
|---|---|---|
| 0x13 (19) | `GetFanSensors` | 风扇关联传感器枚举 |
| 0x14 (20) | `Thermal_Information` | 读传感器温度 / 风扇转速 / 百分比 |
| 0x15 (21) | `Thermal_Control` | 设散热模式 / 设风扇转速百分比 |

参数编码（32 位打包进一个 `arg2`）：

```
(sensorId << 8) | 4      → 读温度 ℃
(fanId    << 8) | 5      → 读风扇 RPM
(fanId    << 8) | 6      → 读风扇百分比
(thermalMode << 8) | 1   → 设置散热模式（Thermal_Control）
(addonPct << 16) | (fanId << 8) | 2   → 设置该风扇转速百分比
```

散热模式枚举：

```
Custom = 0            Quiet  = 150       Balanced = 151
Performance = 152     FullSpeed = 153
USTT_Balanced = 160   USTT_Cool = 162    USTT_FullSpeed = 164
G_Mode = 171
```

风扇 ID 区间 49–99，传感器 1–48。这些就是「改成固定高转速」所需要的全部参数。

---

## 5. 与本机核对

本机 `IOACPIPlane` 中的 ACPI-WMI（`PNP0C14`）设备共 **10 个**：

```
AWMI   WFDE   WFTE   AMW0   AMW2
AMW5   AMW4   DIAG   AMWV   AMWW
```

其中：

- **`AWMI`** —— Alienware 的 WMI 主接口（Linux 里 `alienware-wmi` 驱动就绑它）；
- **`AMWW`** —— 正好是 alien-tools v1 探测名单里的名字（`\_SB.AMWW.WMAX`）；
- `AMW0` —— Dell 传统 ACPI-WMI 设备（热键/WMI 事件）。

对照之前的诊断：`SMCDellSensors` 要的是 i8k **SMM 签名**，12 代 BIOS 不再暴露 → kext 初始化失败 → `FNum = 0`、SMC 里没有任何 `F0*` 键。**那是另一条路，和这里的 WMI 路线互不相干。** 结论修正为：不是「硬件没给风扇接口」，而是「macOS 走了错的接口」。

---

## 6. 为什么它不能直接在 macOS 上跑

| 障碍 | 说明 |
|---|---|
| 没有 WMI/CIM | macOS 里不存在 `root\WMI`，也没有 COM WMI provider，`AWCCWmiMethodFunction` 这个类根本无从访问 |
| 没有 PE 加载器 | 两个 exe 都是 PE32+，XNU 不加载 |
| v1 的驱动 | `HwAcc.sys` 是 Windows 内核驱动（KMDF/`\\.\ACPI`），无法在 XNU 里加载 |
| 权限模型 | 即使能跑，也需要 Windows 侧的 ACL / 驱动签名体系 |

但 macOS **有** ACPI 子系统，`AMWW` / `AWMI` 这两个设备已经在 `IOACPIPlane` 里注册成功。这正是可操作空间所在。

---

## 7. 三条落地路径（成本递增）

### A. Windows 一次性设模式，验证是否跨系统保持（15 分钟，先试这个）

1. 进 Windows，装 `alien-tools.zip`（或官方 AWCC）；
2. 用 GUI 选 **Full Speed / Performance**，或按 **Fn+F1**（m15 R7 的高性能热键），
   也可命令行：`alienfan-cli setfans=0,100`（把 0 号风扇拉到 100%）；
3. 保持该状态 **直接重启进 macOS**（不要断电关机）；
4. 在 macOS 里听风扇基线 / 看是否能感到转速变化。

判断依据：若 EC 的散热模式跨系统保留 → 目的达成，零成本。若重启即回到默认 → 说明 AWCC 每次开机重新下发，路径 A 只能算「临时」。

### B. BIOS 侧散热选项（低成本）

Alienware 的 BIOS（F2）通常没有完整风扇曲线，但可能有 Performance / Thermal 相关项。配合 Fn+F1 的固件级处理，部分机型在无 AWCC 时也生效——值得进去看一眼实测。

### C. 写一个 macOS kext 去调 `\_SB.AMWW`（工程量大，但可行）

思路：Lilu 系 kext，用 `IOACPIPlatformDevice::evaluateObject` 对 `AMWW` 求值 `WMAX`/`ICPC`/`SETC`/`SETB`，参数编码照抄第 4 节；把结果通过 VirtualSMC 暴露成 `F0Ac`/`F0Tg`/`F0Mn`/`F0Mx` 键，Macs Fan Control 就能直接用。

- 优点：唯一能做到「macOS 原生、按曲线控制」的路；
- 代价：需要逐个方法试探签名（`WMAX` 返回能力位图，`ICPC` 初始化命令包，`SETC`/`SETB` 提交），失败时可能写坏 EC 状态；
- 前提：必须能从 DSDT 里读出方法签名（需要一份 ACPI dump），否则只能黑盒试。

---

## 8. 风险与安全提醒

- **v1 路线要关驱动黑名单并加载未签名内核驱动**，等价于给系统开一个提权后门，杀软报警是正常反应。不要为了控风扇在常用系统上长期保留。
- **GUI 带 WININET 联网**（检查更新）。功能上无害，但如果你在意「不联网」，用 CLI 版。
- **风扇控制永远有「写错就停转」的风险**。这个工具本身有兜底（BIOS 仍会监控温度并在过热时强制提速），自己写 kext 时**没有**这层保护。
- 所有 ACPI/EC 写入都是**不可逆的固件状态变更**，出问题的最坏结果是风扇不转 → 过热 → 硬件损伤。

---

## 附：本次产物

```
rea-awcc/
├── alien-tools-master/     源码（含 alienfan-SDK 两代实现）
├── tcc-g15-master/         另一实现（Python + WMI，含 WMI-AWCC-doc.md）
├── bin/tools/              新版 release（exe + 文档）
├── bin/v1/                 v1 release（Direct ACPI 版）
└── AWCC-第三方风扇工具逆向分析.md
```
