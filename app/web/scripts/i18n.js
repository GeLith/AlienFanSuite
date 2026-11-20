/*
 * i18n.js — 界面语言包
 *
 * 中文是源码语言。切换到英文时，遍历文本节点，按字典替换；
 * 每个节点缓存原始中文，因此可以随时来回切换。
 * 动态文案用 I18N.t('中文') 直接翻译。
 */
(function () {
  'use strict';

  var DICT = {
    /* 顶栏 / 导航 */
    '连接中': 'Connecting',
    '最高温度': 'Max Temp',
    '启用曲线': 'Start Curve',
    '停用曲线': 'Stop Curve',
    '曲线调速引擎': 'Curve Engine',
    '运行中 · 按当前曲线实时调节转速': 'Running · adjusting speed in real time by current curve',
    '未启用 · 风扇按开机 / BIOS 策略运转': 'Stopped · fans follow boot / BIOS policy',
    '已记住上次选择': 'Last choice remembered',
    '控制台': 'Dashboard',
    '曲线编辑': 'Curve Editor',
    '预设方案': 'Profiles',
    '高级 / 协议': 'Advanced / Protocol',
    '设置': 'Settings',
    '驱动未连接': 'Driver not connected',
    '初始化…': 'Initializing…',

    /* 控制台 */
    '实时控制台': 'Live Telemetry',
    '读数直接来自 EC 的 WMAX 命令。boost 是叠加在 BIOS 曲线上的加速量，100% 时顶到 4800 RPM 上限。':
      'Readings come straight from the EC via WMAX. Boost is added on top of the BIOS curve; 100% reaches the 4800 RPM limit.',
    '处理器': 'CPU',
    '显卡': 'GPU',
    '当前模式': 'Current Mode',
    '目标转速': 'Target Speed',
    '多级调节': 'Multi-Level Control',
    '当前档位': 'Level',
    '· 点击档位立即下发': '· click a level to apply instantly',
    '静音': 'Quiet',
    '均衡': 'Balanced',
    '性能': 'Performance',
    '强冷': 'Cooling',
    '满转': 'Full Speed',
    '二进制备注 · boost 写入': 'Binary Note · boost write',
    '= 写入命令；': '= write command;',
    '= 设置 boost 百分比；': '= set boost percent;',
    '= 风扇 ID（': '= fan ID (',
    '），0 表示两个风扇；': '), 0 = both fans;',
    '= 百分比 0–100。下发值会叠加在 BIOS 的温控曲线之上。':
      '= percent 0-100. The value is added on top of the BIOS thermal curve.',

    /* 曲线 */
    '温度 / 转速曲线': 'Temperature / Fan Curve',
    '横轴为最高温度（30–100°C），纵轴为 boost（0–100%）。拖动圆点调整；双击空白加点；右键圆点删除。':
      'X axis: hottest temperature (30–100°C). Y axis: boost (0–100%). Drag points, double-click empty space to add, right-click a point to remove.',
    '当前曲线': 'Active Curve',
    '实时工作点': 'Live Operating Point',
    '载入预设': 'Load Profile',
    '立即应用': 'Apply Now',
    '最高温': 'Hottest',
    '曲线引擎': 'Curve Engine',
    '每': 'Every',
    '秒取最高温度，线性插值得到目标 boost；单次变化限制':
      ' s: sample the hottest sensor, interpolate the target boost; max change per step ',
    '%，避免风扇忽快忽慢。切散热模式会清空 boost，曲线运行时下一拍自动补发。':
      '%, avoiding abrupt fan speed changes. Switching thermal mode clears boost; a running curve re-sends it on the next tick.',
    '引擎参数': 'Engine Parameters',
    '采样间隔': 'Sample Interval',
    '单次限幅': 'Max Step',

    /* 预设 */
    '预设保存在': 'Profiles are stored in',
    '。载入后立即生效。': '. Loading takes effect immediately.',
    '打开配置目录': 'Open Config Folder',
    '恢复内置预设': 'Reset Built-in Profiles',
    '温度曲线': 'Temp curve',
    '固定 ': 'Fixed ',
    ' 个控制点': ' control points',
    '模式 ': 'Mode ',
    'G 模式 ': 'G Mode ',
    '保持': 'Keep',
    '开': 'On',
    '关': 'Off',
    '载入': 'Load',
    '删除': 'Delete',
    '自定义': 'Custom',

    /* 高级 / 协议 */
    '所有十六进制值均附中文注解。原始 WMAX 仅用于调试，误操作可能导致风扇停转。':
      'Every hex value has a Chinese note in the original UI. Raw WMAX is for debugging only; misuse may stop the fans.',
    '散热模式': 'Thermal Modes',
    '模式码来自 EC 的 powers[] 表。不同档位对应不同 BIOS 温控基线；切换会清空 boost，可在曲线页重新应用。':
      'Mode codes come from the EC powers[] table. Each level has a different BIOS thermal baseline; switching clears boost, re-apply from the curve page.',
    '二进制备注 · 模式': 'Binary Note · Mode',
    '= 设置散热模式；': '= set thermal mode;',
    '= 模式原始码（USTT 表）：': '= raw mode code (USTT table):',
    '自定义/手动、': 'Custom/Manual, ',
    '平衡、': 'Balanced, ',
    '平衡-性能、': 'Balanced-Performance, ',
    '冷却、': 'Cooling, ',
    '安静、': 'Quiet, ',
    '性能、': 'Performance, ',
    '低功耗、': 'Low Power, ',
    'G 模式。': 'G Mode.',
    '系统开关': 'System Switches',
    'G 模式': 'G Mode',
    '高性能散热策略': 'High-performance thermal policy',
    '二进制备注 · G 模式': 'Binary Note · G Mode',
    '读取状态；写入': 'read status; write ',
    '兜底。本机命令有效但读回 0，故按“支持”处理。':
      ' as fallback. The command works on this model but reads back 0, so it is treated as supported.',
    '引擎控制': 'Engine Control',
    '恢复 BIOS 控制': 'Restore BIOS Control',
    '原始 WMAX 命令': 'Raw WMAX Command',
    '直接构造 EC 调用。参数支持 0x 开头的十六进制或十进制。':
      'Build an EC call directly. Arguments accept 0x-prefixed hex or decimal.',
    'com · 命令': 'com · command',
    'sub · 子命令': 'sub · sub-command',
    'a1 · 参数1': 'a1 · arg 1',
    'a2 · 参数2': 'a2 · arg 2',
    '执行': 'Run',
    '结果：': 'Result: ',
    '· 示例：读 FAN0 转速 = com 0x14 / sub 0x05 / a1 0x32':
      '· example: read FAN0 RPM = com 0x14 / sub 0x05 / a1 0x32',
    '命令速查': 'Command Cheat Sheet',
    '读取 ·': 'read ·',
    '写入 ·': 'write ·',
    '系统信息 ·': 'system info ·',
    '风扇 ID（a1=0→0x32，a1=1→0x33）': 'fan ID (a1=0→0x32, a1=1→0x33)',
    '转速 RPM（a1 必须传 ID 而非下标）': 'RPM (a1 must be the ID, not an index)',
    '最大转速 ·': 'max RPM ·',
    '温度 ·': 'temperature ·',
    '百分比读数（本机不支持，恒返回 0xFE）': 'percent read (unsupported on this model, always 0xFE)',

    /* 设置 */
    '设置与驱动状态': 'Settings & Driver Status',
    '控制软件与驱动按版本配对发布。可随时重新检测驱动，或切换界面语言。':
      'The app and driver are released as a version-matched pair. Re-check the driver or switch the UI language at any time.',
    '驱动绑定': 'Driver Binding',
    '检测中…': 'Checking…',
    '控制软件': 'App',
    'AlienFan 驱动': 'AlienFan Driver',
    '要求版本': 'Required',
    '重新检测': 'Re-check',
    '正在读取 kext 与 UserClient 状态…': 'Reading kext and UserClient status…',
    '驱动状态': 'Driver Status',
    'kext 已安装': 'kext installed',
    '内核已加载': 'Loaded in kernel',
    '设备就绪': 'Device ready',
    'UserClient 连接': 'UserClient connection',
    '匹配方式': 'Match method',
    'ACPI 设备': 'ACPI device',
    '风扇 / 传感器 / 模式': 'Fans / Sensors / Modes',
    '当前 boost / 模式': 'Current boost / mode',

    /* 启动与状态栏 */
    '启动与状态栏': 'Startup & Menu Bar',
    '开机自启以后台方式运行，仅在菜单栏显示温度；也可单独设置启动时最小化。设置立即保存到本机偏好。':
      'Launch at login runs in the background and shows temperature only in the menu bar; you can also set start-minimized separately. Changes save instantly to local preferences.',
    '开机自启': 'Launch at Login',
    '登录时自动后台启动，仅驻留菜单栏': 'Auto-start in background at login, menu bar only',
    '启动时最小化': 'Start Minimized',
    '手动启动时也隐藏主窗口，仅状态栏显示': 'Hide the main window on manual launch too, status bar only',
    '状态栏显示温度': 'Menu Bar Temperature',
    '选择菜单栏图标上显示的温度来源，悬浮提示始终显示 CPU 与显卡两个读数。':
      'Choose the temperature source shown on the menu bar icon; the tooltip always shows both CPU and GPU readings.',
    '已开启开机自启': 'Launch at login enabled',
    '已关闭开机自启': 'Launch at login disabled',
    '已开启启动最小化': 'Start minimized enabled',
    '已关闭启动最小化': 'Start minimized disabled',
    '状态栏温度已切换': 'Menu bar temperature switched',

    '界面语言': 'Language',
    '切换后立即生效，选择会保存在本机偏好中。': 'Takes effect immediately; saved in local preferences.',
    '关于': 'About',
    'ACPI/WMI · WMAX 0x14 / 0x15 · 戴尔 / Alienware 全系 AWCC 机型通用（已在 m15 R7 验证）':
      'ACPI/WMI · WMAX 0x14 / 0x15 · works with all Dell / Alienware AWCC models (verified on m15 R7)',
    '协议参考：': 'Protocol reference: ',
    '项目主页：': 'Project home: ',
    '见 GitHub 仓库': 'see GitHub repository',
    '安全提示': 'Safety Note',
    '写入命令直接作用于 EC。误用原始 WMAX 可能导致风扇停转，请优先使用曲线与预设。':
      'Write commands act directly on the EC. Misusing raw WMAX may stop the fans; prefer the curve and profiles.',
    '已安装': 'Installed',
    '未安装': 'Not installed',
    '已加载': 'Loaded',
    '未加载': 'Not loaded',
    '是': 'Yes',
    '否': 'No',
    '未知': 'Unknown',
    '正常': 'OK',
    '版本匹配': 'Versions matched',
    '版本不匹配': 'Version mismatch',
    '驱动未安装：请先安装随附的 AlienFan.kext。': 'Driver not installed: install the bundled AlienFan.kext first.',
    '驱动已安装但内核未加载：重启或批准系统扩展后重试。':
      'Driver installed but not loaded: reboot or approve the system extension, then retry.',
    '驱动版本过低或不兼容：请使用与本软件一同发布的驱动。':
      'Driver version is too old or incompatible: use the driver bundled with this app.',
    '驱动已就绪，可以正常使用。': 'Driver is ready.',
    '读取驱动诊断信息失败。': 'Failed to read driver diagnostics.',
    '失败': 'Failed',
    '成功': 'OK',

    /* 模式名 */
    '自定义 / 手动': 'Custom / Manual',
    '安静（旧款）': 'Quiet (legacy)',
    '平衡（旧款）': 'Balanced (legacy)',
    '性能（旧款）': 'Performance (legacy)',
    '全速（旧款）': 'Full Speed (legacy)',
    '平衡': 'Balanced',
    '平衡-性能': 'Balanced-Performance',
    '冷却': 'Cooling',
    '安静': 'Quiet',
    '低功耗': 'Low Power',
    '档位 ': 'Level ',
    '● 当前': '● Current',
    '读取模式表失败。': 'Failed to read the mode table.',

    /* 动态提示 */
    '曲线已更新，点「立即应用」生效': 'Curve updated; click Apply Now to take effect',
    '至少保留两个控制点': 'Keep at least two control points',
    '已切到 ': 'Switched to ',
    '，引擎下一拍补发 boost': ', boost will be re-sent next tick',
    '曲线已停用': 'Curve stopped',
    '曲线已启用': 'Curve started',
    '已下发 ': 'Applied ',
    '% 到两个风扇': '% to both fans',
    '已切到手动档位，曲线已停用': 'Manual level applied, curve stopped',
    '，曲线运行中，下一拍补发 boost': ', curve running, boost re-sent next tick',
    '，曲线未运行，请到控制台设定档位': ', curve stopped — set a level from the dashboard',
    '· 曲线接管中，点击将切为手动': '· curve active, clicking switches to manual',
    '曲线已应用': 'Curve applied',
    '已载入 ': 'Loaded ',
    '恢复内置预设？当前自定义预设会被覆盖。': 'Reset built-in profiles? Custom profiles will be overwritten.',
    '已恢复内置预设': 'Built-in profiles restored',
    '删除预设「': 'Delete profile "',
    '」？': '"?',
    '已删除': 'Deleted',
    'G 模式已开启': 'G Mode on',
    'G 模式已关闭': 'G Mode off',
    '恢复 BIOS 控制？boost 将归 0。': 'Restore BIOS control? Boost will be set to 0.',
    '已交还 BIOS 控制': 'BIOS control restored',
    '输入预设名称：\n已有：': 'Profile name:\nExisting: ',
    '错误：': 'Error: ',
    '调用失败': 'Call failed',
    '已连接 · ': 'Connected · ',
    ' 风扇 · ': ' fans · ',
    ' 传感器 · ': ' sensors · ',
    ' 档模式': ' modes',
    '未知错误': 'Unknown error',
    '重新检测完成': 'Re-check complete'
  };

  var lang = 'zh';

  function t(s) {
    if (lang === 'zh' || s == null) return s;
    var str = String(s);
    var key = str.trim();
    if (Object.prototype.hasOwnProperty.call(DICT, key)) {
      return str.replace(key, DICT[key]);
    }
    return str;
  }

  /* 遍历文本节点；原文缓存在 __afZh 上，保证可来回切换 */
  function apply(node) {
    var n = node || document.body;
    if (!n) return;
    if (n.nodeType === 3) {
      if (n.__afZh == null) n.__afZh = n.nodeValue;
      var next = t(n.__afZh);
      if (n.nodeValue !== next) n.nodeValue = next;
      return;
    }
    if (n.nodeType === 1) {
      ['title', 'placeholder'].forEach(function (attr) {
        var v = n.getAttribute(attr);
        if (v == null) return;
        if (!n.__afAttrs) n.__afAttrs = {};
        if (n.__afAttrs[attr] == null) n.__afAttrs[attr] = v;
        var nv = t(n.__afAttrs[attr]);
        if (nv !== v) n.setAttribute(attr, nv);
      });
    }
    var child = n.firstChild;
    while (child) {
      var sibling = child.nextSibling;
      apply(child);
      child = sibling;
    }
  }

  function set(next) {
    lang = next === 'en' ? 'en' : 'zh';
    document.documentElement.lang = lang === 'zh' ? 'zh-CN' : 'en';
    apply(document.body);
    window.dispatchEvent(new CustomEvent('aflang', { detail: { language: lang } }));
  }

  /* 动态写入文本：保留中文原文，语言切换时可还原 */
  function setText(el, zh) {
    if (!el) return;
    el.textContent = t(zh);
    var n = el.firstChild;
    if (n && n.nodeType === 3) n.__afZh = zh;
  }

  /* 先用中文构建 DOM 并缓存原文，再按当前语言渲染 */
  function withZh(fn) {
    var prev = lang;
    lang = 'zh';
    try {
      fn();
      apply(document.body);
    } finally {
      lang = prev;
    }
    if (prev !== 'zh') apply(document.body);
  }

  function setHTML(el, html) {
    if (!el) return;
    withZh(function () { el.innerHTML = html; });
  }

  window.I18N = {
    t: t,
    apply: apply,
    set: set,
    setText: setText,
    setHTML: setHTML,
    withZh: withZh,
    get: function () { return lang; }
  };
})();
