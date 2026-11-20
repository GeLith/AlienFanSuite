# Fan Control — Alienware 风扇控制台

原生 macOS 应用（WKWebView 壳 + 设计化 Web UI），直接通过 `AlienFan.kext` 的 UserClient 读写风扇，无需 root、无需后台服务。

当前版本 **1.3.0**，与 **AlienFan.kext 2.1.0** 配对发布；设置页会校验二者版本是否匹配。

## 设计方向

**工业仪表 / 精密仪器**：碳黑底 + Alienware 红强调 + 冷青数据色，等宽数字，发丝边框，网格与噪点氛围层。

| 项目 | 选择 |
|---|---|
| 展示字 | Chakra Petch（半粗/粗，字距 −0.03em，大写标题） |
| 数据字 | JetBrains Mono（等宽 `tabular-nums`） |
| 中文 | 系统苹方 |
| 主色 / 强调 | `#0b0d10` / `#e11c2d` |
| 次要数据色 | 冷青 `#38d6e8` |
| 记忆点 | 80px 巨型 RPM 数字 + 每个十六进制命令旁的中文注解 |

字体文件随 app 一起打包，完全离线可用（OFL 许可证见 `web/fonts/`）。

## 五个页面

### 01 控制台
- **曲线引擎主控**：启用/停用开关与运行指示灯在控制台首页，状态说明明确区分「曲线实时调速」与「开机 / BIOS 策略」；启停状态跨重启记忆并自动恢复，按钮显示与实际运行严格一致
- 两个风扇的巨型 RPM 读数、boost、转速进度条与刻度
- CPU / GPU 温度，按 60°C / 75°C 自动切换冷青 / 琥珀 / 红
- **多级调节**：五档（30 / 50 / 70 / 85 / 100%），点击立即下发给两个风扇
- 档位下方附 `0x15 / 0x02` 写入命令的逐字节中文注解

### 02 曲线编辑
- SVG 曲线：横轴最高温度 30–100°C，纵轴 boost 0–100%
- 拖动圆点调整；双击空白加点；右键圆点删除（至少保留两点）
- 青色竖线 + 圆点 = 实时工作点
- 可调采样间隔（1–5s）与单次限幅（1–30%）
- 引擎取最高温度插值，切模式后下一拍自动补发 boost

### 03 预设方案
- 内置静音 / 均衡 / 性能，默认选中「性能」
- 载入立即生效；支持删除、恢复内置
- 保存于 `~/Library/Application Support/AlienFan/presets.json`

### 04 高级 / 协议
- 散热模式列表，每个模式码都标注中文含义（依据 USTT 表，详见下方）
- G 模式开关、停用曲线、恢复 BIOS 控制
- 原始 WMAX 控制台 + 命令速查表，所有十六进制值逐项注解

### 05 设置
- 驱动绑定：App 版本 / kext 版本 / 要求版本，兼容状态徽标
- 驱动状态：kext 安装、内核加载、设备 Ready、UserClient、匹配方式、ACPI 设备、System ID、读数计数、当前 boost/模式，附 kext 的 `Diag` 探测轨迹
- 「重新检测」按钮随时刷新；不兼容时给出对应处理提示
- 启动与状态栏：
  - 开机自启：写入用户级 `~/Library/LaunchAgents/com.dongdong.FanControl.plist`，带 `--background` 参数后台启动、隐藏 Dock 图标、仅驻留菜单栏（无需管理员权限）
  - 启动时最小化：手动启动时也隐藏主窗口，仅状态栏显示
  - 状态栏温度来源：最高温 / CPU (`temps[0]`) / 显卡 (`temps[1]`) 三选一，悬浮提示始终显示两个读数；菜单栏右键子菜单可快速切换
  - 以上偏好写入 `NSUserDefaults`，菜单标题与设置页实时回显
- 界面语言：简体中文 / English 一键切换，写入 `NSUserDefaults`，菜单栏与窗口标题同步跟随

## 与开机满转的关系

kext 的 `AlienFanBootBoost = 100`，所以：

- 不打开 App / 打开但不启用曲线 → 开机满转（约 4800 RPM）
- 点「启用曲线」→ 按当前预设接管
- 点「多级调节」任一档 → 立即固定到该 boost
- 点「恢复 BIOS 控制」→ boost 归 0；下次重启恢复满转

## 架构

```
AppDelegate.m   原生壳：窗口、WKWebView、菜单栏
AFBridge.m      JS ↔ 引擎桥接（getState / setBoost / setPower / raw / 引擎 / 预设）
curve.m/h       曲线引擎 + 预设持久化
afclient.c/h    驱动通信库（复用 alienfanctl 的选择器）
web/            HTML/CSS/JS + 打包字体
  styles/tokens.css    设计令牌（颜色/字阶/间距/动效）
  styles/base.css      重置、@font-face、焦点与降级动效
  styles/components.css 全部组件样式
  scripts/bridge.js    Promise 化桥接客户端
  scripts/i18n.js      中英文词典 + 文本节点翻译/还原
  scripts/app.js       渲染与交互
```

桥接协议：JS 调 `window.webkit.messageHandlers.af.postMessage({id,cmd,args})`，
原生回 `window.__afReply(id, reply)`，每秒推 `window.__afPush(state)`。

## 重新编译

```bash
cd FanControl && bash build.sh     # 生成 build/FanControl.app（含 web 资源）
```

只依赖 Command Line Tools（clang + Cocoa + WebKit），不需要 Xcode 工程。

---

## 散热模式码中文对照（USTT）

本机 powers[] 原始码 → 中文名，依据 **Linux 内核官方文档** `alienware-wmi`
（https://dri.freedesktop.org/docs/drm/wmi/devices/alienware-wmi.html）的 USTT 表，
并已与 tcc-g15 的 WMI-AWCC 文档交叉核对。

| 原始码 | 官方名称 | 中文 |
|---|---|---|
| `0x00` | Custom | 自定义 / 手动 |
| `0x96` | Quiet (Legacy) | 安静（旧款） |
| `0x97` | Balanced (Legacy) | 平衡（旧款） |
| `0x98` | Performance (Legacy) | 性能（旧款） |
| `0x99` | Full Speed (Legacy) | 全速（旧款） |
| `0xA0` | Balanced USTT | **平衡**（本机当前） |
| `0xA1` | Balanced Performance USTT | 平衡-性能 |
| `0xA2` | Cool USTT | 冷却 |
| `0xA3` | Quiet USTT | 安静 |
| `0xA4` | Performance USTT | 性能 |
| `0xA5` | Low Power USTT | 低功耗 |
| `0xAB` | G-Mode | G 模式 |

本机实测 powers 表下标：`[0]=0x00、[1]=0xA0、[2]=0xA1、[3]=0xA5、[4]=0xA3`。
注意 USTT 的 `0xA3=安静`、`0xA5=低功耗` 与旧版 GUI 里写的「0xA3 自定义 / 0xA5 性能」相反——旧注释是错的，已修正。

> 注：AWCC 的 Legacy 组（0x96–0x99）和 USTT 组（0xA0–0xA5）二选一，
> 支持 USTT 的机型（如 m15 R7）不使用 Legacy 组。

命令行直接下发原始码（绕过下标查表）：`alienfanctl mode 160` = 0xA0 平衡。

---

## 曲线坐标轴说明

- **X 轴（横轴）= 最高温度**：取 CPU / GPU 传感器中的最高值，范围 `30–100°C`
- **Y 轴（纵轴）= boost 强度**：叠加在 BIOS 曲线之上的加速百分比 `0–100%`，
  引擎按 `RPM ≈ MaxRPM × boost%` 下发给风扇（上限 4800 RPM）

曲线含义：温度越高 → 目标 boost 越大 → 风扇转得越快。拖动圆点改某个温度下的 boost，
双击空白加点，右键圆点删除。图上青色竖线/圆点是实时工作点，Y 轴已标注 `0/25/50/75/100%` 刻度。

三套内置预设均为 **12 个控制点**（30、37、44、51、58、64、70、76、82、88、94、100°C），
比旧的 3–4 点密集得多，插值更平滑。已有 presets.json 会在首次启动时一次性升级到 12 点
（只替换曲线点，保留模式/boost 等设置；标记 `AlienFanCurveDenseV2` 保证只升一次）。

---

## 应用图标

手绘矢量图标（Core Graphics 绘制，`draw_icon.swift`），风格与软件 UI 一致：
碳黑圆角底 + Alienware 红三叶风扇 + 冷青刻度环 + 顶部红色高温警示弧。

- 源脚本：`draw_icon.swift`（`swiftc draw_icon.swift -o draw && ./draw out.png`）
- 预览图：`icon-preview.png`（1024×1024）
- 图标资源：`FanControl.icns`（含 16→1024 全尺寸，由 `iconutil` 打包）
- `build.sh` 会把 icns 拷进 app Resources，`Info.plist` 的 `CFBundleIconFile` 指向它
