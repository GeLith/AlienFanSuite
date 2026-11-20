# AlienFan.kext

macOS 下的 Alienware / Dell 风扇控制驱动。走 **ACPI** 通道，不碰 SMC、不依赖 SMCDellSensors。

**这一版是把 [T-Troll/alien-tools](https://github.com/T-Troll/alien-tools) 的 v1 SDK（Windows 那套）整体移植过来的**，不是另起炉灶。目标是让 macOS 上的能力对齐 Windows 端的 `alienfan-cli`。

---

## 1. 移植对照表

| Windows 端（alienfan-SDK） | macOS 端（本驱动） | 说明 |
|---|---|---|
| `EvalAcpiMethod(hDriver, "\\_SB.AMWW.WMAX", …)` | `IOACPIPlatformDevice::evaluateObject("WMAX", …)` | 进 ACPI 的方式不同，参数形状完全一致 |
| 自带 `HwAcc.sys` + `DeviceIoControl` | 不需要 | XNU 不允许，直接用 IOACPI 族 |
| `devs[]` 五条候选设备 | `kDevices[]`，同样五条、同样顺序 | 逐条探测，谁应答用谁 |
| `dev_controls` 14 条命令 | `kControls[]`，同样 14 条、同样顺序 | 见下方协议表 |
| `delta`（Aurora/Area51 为 4） | `fDelta`，同样参与 `com - delta` | 笔记本恒为 0 |
| `Control::Probe()` | `AlienFan::probeCurrent()` | 枚举顺序、终止条件逐行对齐 |
| `Control::RunMainCommand()` | `AlienFan::runMain()` | |
| `SetFanBoost(fanID, value)` | `setFanBoost(fan, value)` | 语义同为 **boost 百分比** |
| `SetPower(level)` | `setPower(level)` | 语义同为 **powers[] 下标** |
| `dump` 命令 | `alienfanctl dump` | 现场摸协议用 |

两处**有意不同**：

1. 上游 `SetGMode` 会先 `eval \_SB.PCI0.LPC0.EC0._Q14` 探路。直接求值 EC 查询方法副作用不可控，这里改成只在 `SetGMode` 失败时回退「把 power 设成 0xAB」。
2. 上游还会读 `SEN0._STR` / `ECDV` / `KDRT` 这些 EC 直读温度。macOS 上没有对应节点，这里只保留 AWCC 温度通道。

---

## 2. 协议

```
result = <device>.WMAX( int 0, int (com - delta), buffer[4]{ sub, arg1, arg2, 0 } )
```

| 序号 | com | sub | arg1 | arg2 | 含义 |
|---|---|---|---|---|---|
| 1 | 0x14 | 0x03 | 索引 | – | 取 PowerID / 风扇 / 传感器 / 模式枚举 |
| 2 | 0x14 | 0x05 | fanId | – | 读 RPM |
| 3 | 0x14 | 0x06 | fanId | – | 读百分比 |
| 4 | 0x14 | 0x0c | fanId | – | 读当前 boost |
| 5 | 0x15 | 0x02 | fanId | 0-100 | **设置 boost** |
| 6 | 0x14 | 0x04 | 传感器ID | – | 读温度 |
| 7 | 0x14 | 0x0b | – | – | 读当前散热模式 |
| 8 | 0x15 | 0x01 | 模式下标 | – | **设置散热模式** |
| 9 | 0x13 | 0x04 | – | – | GPU 功耗限制 |
| 10 | 0x25 | 0x02 | – | – | 读 G 模式 |
| 11 | 0x25 | 0x01 | 0/1 | – | 切 G 模式 |
| 12 | 0x1a | 0x02 | 2 | – | 读 system ID |
| 13 | 0x13 | 0x02 | fanId | – | 取风扇对应的传感器 |
| 14 | 0x14 | 0x09 | fanId | – | 读最大 RPM |

### 两个容易搞错的语义

**boost 不是转速。** `0x15/0x02` 传的是「在 BIOS 风扇曲线之上额外加速的百分比」，`0` = 保持 BIOS 原策略，`100` = 加到最猛。它不是绝对 RPM，也**不会让风扇停转**——所以驱动里去掉了旧版的「最低 20%」限制。

**setPower 传的是下标。** 不是 150/151/152/153 那些原始码。驱动启动时用 `(0x14,3)` 把模式表枚举出来，`powers[0]` 固定是 `0`（unlock），后面依次是 BIOS 实际的档位。`0xAB`（171）是 G 模式，单独透传。

### 枚举的边界条件（照抄上游，别随手简化）

```
风扇:  while ((id = getPowerID(i)) < 0x100 && id > 0) || id > 0x130
传感器: while ((id = getPowerID(i)) > 0x100 && id < 0x1A0)
模式:   while ((id = getPowerID(i)) && id > 0)
```

`id > 0x130` 那半段是给 m15 R7 上那个 `0x132` 的风扇留的——写成 `id > 0` 会直接漏掉它。

---

## 3. 构建

只要 Command Line Tools，不需要完整 Xcode：

```bash
cd AlienFan
./build.sh
```

产物：

```
build/AlienFan.kext/Contents/MacOS/AlienFan   Mach-O 64-bit kext bundle x86_64
build/alienfanctl                             CLI
```

已验证：编译通过、`kmutil libraries` 校验无缺失依赖（需 `com.apple.iokit.IOACPIFamily 1.4`）。

---

## 4. 安装

### 方式 A：临时验证

```bash
sudo chown -R root:wheel build/AlienFan.kext
sudo kmutil load -p build/AlienFan.kext
log show --last 2m --predicate 'eventSender == "AlienFan"' --info | tail -20
```

卸载：`sudo kmutil unload -b com.dongdong.AlienFan`

> 本机 SIP 的 `Kext Signing` 已是 disabled，可以直接加载未签名 kext。

### 方式 B：持久安装（OpenCore 注入，推荐）

1. 挂载 ESP，拷到 `EFI/OC/Kexts/AlienFan.kext`
2. `config.plist` → `Kernel → Add` 加一条：

```xml
<dict>
    <key>Arch</key><string>x86_64</string>
    <key>BundlePath</key><string>AlienFan.kext</string>
    <key>Comment</key><string>AlienFan ACPI fan control</string>
    <key>Enabled</key><true/>
    <key>ExecutablePath</key><string>Contents/MacOS/AlienFan</string>
    <key>MaxKernel</key><string></string>
    <key>MinKernel</key><string></string>
    <key>PlistPath</key><string>Contents/Info.plist</string>
</dict>
```

**回滚**：`Enabled` 改 `<false/>` 重启即可。不写固件、不改 NVRAM。

---

## 5. 用法

```bash
sudo ./alienfanctl info        # 探测到了哪个设备、systemID、各项计数
sudo ./alienfanctl status      # 全量：风扇 + 温度 + 模式表
sudo ./alienfanctl powers      # 模式表（下标 -> 原始值）
sudo ./alienfanctl list
sudo ./alienfanctl sensors
```

**只想让风扇更积极，最短路径：**

```bash
sudo ./alienfanctl mode unlock     # 先解锁，否则手动 boost 会被 BIOS 压回去
sudo ./alienfanctl boost all 100   # boost 拉满
sudo ./alienfanctl boost all 0     # 不想要了，恢复 BIOS 原策略
```

或者用 BIOS 预设档（跑 `powers` 看你的机器有哪几档）：

```bash
sudo ./alienfanctl mode 1     # 具体含义看 powers 输出
sudo ./alienfanctl mode gmode # G 模式
```

> 官方警告沿用：设成非零 power level 通常会**禁用手动 boost**，两者用其一。

**现场摸协议（最有用）：**

```bash
sudo ./alienfanctl raw 0x14 3 0 0   # 单个组合
sudo ./alienfanctl dump             # 扫 0x13/0x14/0x25/0x1a 全部子命令
sudo ./alienfanctl dump --write     # 连 0x15 一起扫（会真的下发写命令，慎用）
sudo ./alienfanctl reprobe
```

开机自动生效，改 `Info.plist` 里的 personality：

```xml
<key>AlienFanBootMode</key><integer>-1</integer>   <!-- -1 = 不动；1..N = 对应 powers 下标 -->
<key>AlienFanBootBoost</key><integer>100</integer> <!-- 0 = 不动；开机自动 unlock + 拉满 -->
<key>AlienFanReapplyMs</key><integer>5000</integer><!-- BIOS 若覆盖，每 5s 重下发 -->
```

---

## 6. 安全设计

| 机制 | 说明 |
|---|---|
| 探测失败即静默 | `(0x14,2)` 返回 ≤0 时，一次写都不会发生 |
| 只读开关 | `AlienFanAllowWrite=false` 后所有写请求返回 `kIOReturnNotPermitted`，可用于安全试探测 |
| 默认零动作 | `BootMode=-1`、`BootBoost=0`，出厂配置不改任何散热行为 |
| delta=4 默认关 | 避免对笔记本误发 `0x10/0x11` 这类未定义命令 |
| 有限重试 | 探测最多重试 20 次，不会反复戳 EC |
| 不改固件 | 状态在 EC 里，重启由 BIOS 重新接管 |

---

## 7. 尚未在真机验证的部分

| 项 | 状态 |
|---|---|
| 编译成 kext bundle | ✅ `Mach-O 64-bit kext bundle x86_64` |
| 依赖完整 | ✅ `kmutil libraries` 无缺失 |
| `IOACPIPlane` 里有 `AMWW` 节点 | ✅ ioreg 已确认（AWMI/AMW0/AMW2/AMW4/AMW5/AMWV/AMWW 等 10 个 PNP0C14） |
| `WMAX` 方法是否存在、签名是否匹配 | ⚠️ **未实测**——这是唯一的关键未知数 |
| `(0x14,2)` 探测能否通过 | ⚠️ 未知，加载后看日志第一行 |
| 风扇枚举结果 | ⚠️ 未知，`alienfanctl info` 一看便知 |

**备选 personality**（kext 加载了但 ioreg 里没有 `AlienFan` 服务时）：

```xml
<key>IOProviderClass</key><string>IOACPIPlatformDevice</string>
<key>IONameMatch</key><string>AMWW</string>
```

---

## 8. 排错

```bash
kmutil showloaded | grep -i alienfan                      # 是否加载
log show --last 5m --predicate 'eventSender == "AlienFan"' --info
ioreg -l -w0 | grep -A20 '"IOClass" = "AlienFan"'
```

| 日志/现象 | 判断 |
|---|---|
| `设备 ... 应答（delta=0），systemID=...，风扇 N` | 通了 |
| `暂无设备应答，1s 后重试` | `WMAX` 调不通，或 ACPI 平面未就绪 |
| `探测应答但没枚举到风扇` | `WMAX` 通了但 `(0x14,3)` 枚举条件不符 → 跑 `dump` 看真实返回值 |
| 服务起来但 CLI 找不到 | personality 没匹配，换上面的备选写法 |

---

## 9. 与 Macs Fan Control 的关系

本驱动自己管风扇，**不会**让 Macs Fan Control 里出现风扇——那需要往 VirtualSMC 塞 `F0Ac/F0Tg/F0Mn/F0Mx` 键。要的话下一步写个 SMC 插件把那些键接到这套 ACPI 调用上。

---

## 10. 实机实测记录（Alienware m15 R7 / 黑苹果 macOS 26）

### 10.1 硬件实测参数

| 项目 | 风扇 0 | 风扇 1 |
|---|---|---|
| 风扇 ID | `0x32` | `0x33` |
| 最大转速 | 4800 RPM | 4800 RPM |
| BIOS 默认下转速 | 约 2100 RPM | 约 2400 RPM |

- 温度传感器：2 个（CPU / GPU）
- 散热模式原始码：`0x00(解锁) / 0xa0 / 0xa1 / 0xa5 / 0xa3`
- `0x1a/0x02` 在这台机器上返回 **4800（最大转速）**，不是系统 ID
- `0x14/0x06`（百分比读数）**不支持**，恒返回 `0xfe`。`RPM / MaxRPM` 可正常算百分比
- `0x25/0x02`（G 模式）返回 0，命令本身有效 → 判定支持 G 模式

### 10.2 协议要点（踩过的坑）

**风扇 ID 是作为参数传下去的，不是下标。** 这是最容易错的地方：

```
0x14/0x03  arg1=0 → 50   (0x32)   风扇 0 的 ID
0x14/0x03  arg1=1 → 51   (0x33)   风扇 1 的 ID
0x14/0x03  arg1=2 → 257  (0x101)  越界，枚举到此结束

读转速/最大值必须用「读回来的 ID」：
0x14/0x05  arg1=0x32 → 4784 RPM   ✓
0x14/0x05  arg1=0    → -1         ✗  传下标会失败
```

`0x14/0x03` 同时承担三种用途，靠 `arg1` 的区间区分：

| `arg1` 区间 | 含义 |
|---|---|
| `0 .. n-1` | 风扇 ID（`n` = 风扇数） |
| `n .. m` | 传感器索引（值落在 `0x100`–`0x1A0`） |
| 超过 `m` | 返回负数，枚举终止 |

### 10.3 两个致命 bug（导致风扇数恒为 0）

| 位置 | 问题 | 修法 |
|---|---|---|
| `onTimer()` | 判 `fDeviceIndex == -1` 才探测，但 provider 直连时值是 `-2`，条件不成立 → **整段探测被跳过**，直接标 Ready，风扇恒 0 | 改为 `fDeviceIndex < 0` |
| `doProbe()` | provider 探测成功时 `return fDeviceIndex`（= -2），调用方用 `< 0` 判失败 → 被当成失败反复重试 | 改为 `return 0` |

表象很有欺骗性：kext 显示 `Ready=Yes`、`PowerRaw` 有正常值，但 `FanCount` 属性根本不存在——因为 `probeCurrent()` 一次都没跑过。

### 10.4 boost 实测（关键结论）

`0x15/0x02` 的 `boost` 是**叠加在 BIOS 曲线之上**的百分比，不是绝对转速，
但顶到上限后就基本不随负载变化了：

| 设置 | 实测转速 | 备注 |
|---|---|---|
| 不设（BIOS 默认 0xa0） | 2102 / 2406 | 44–50%，随负载波动 |
| `boost 50` | 2495 / 2742 | |
| `boost 80` | 4175 / 4246 | 约 87% |
| `boost 100` | **4784 / 4796** | 顶到 4800 上限，近似固定满转 |

温度对照：满转后 CPU/GPU 从 `68/65°C` 降到 `46/49°C`。

**不要先 `setPower(0)`（unlock）再 boost。** 上游注释说「先解锁否则禁用手动控制」，
但这台机器上 unlock 反而把 BIOS 基线压低，boost 100% 只能到 4615（vs 4796）。
`applyBootProfile()` 已改成直接下发 boost、不改模式。

切散热模式会**清空 boost**（`mode 3` 到 `0xa5` 后 boost 归 0，转速 4451/4511）。
所以「模式」和「boost」二者取一，不要混着用。

### 10.5 开机自动满转

`Info.plist` 里 `AlienFanBootBoost = 100`，`AlienFanBootMode = -1`（不动模式）。
驱动探测成功后自动下发，无需任何后台进程或 LaunchDaemon。

临时调速（即时生效，重启后回到 100）：

```bash
alienfanctl boost all 60      # 降速
alienfanctl boost all 100     # 满转
alienfanctl boost all 0       # 交还 BIOS 控制
```

注意：`/Library/LaunchDaemons` 与 `/usr/local/bin` 的内联 `osascript` 写入会被系统拦，
必须用「脚本文件 + `with administrator privileges`」的方式提权。
