/* app.js — FanControl 交互与渲染 */
(function () {
  'use strict';

  var $ = function (id) { return document.getElementById(id); };
  /* 发布前把下面的仓库地址换成你的 GitHub 仓库 */
  var REPO_URL = 'https://github.com/GeLith/AlienFanSuite';

  var L = function (s) { return window.I18N ? window.I18N.t(s) : s; };
  var LZ = function (el, zh) { return window.I18N ? window.I18N.setText(el, zh) : (el.textContent = zh); };
  var IH = function (el, html) { return window.I18N ? window.I18N.setHTML(el, html) : (el.innerHTML = html); };

  var TEMP_MIN = 30, TEMP_MAX = 100;
  var curvePoints = [];      /* [{t, b}] 工作副本 */
  var activeName = '';
  var dragging = -1;
  var lastGMode = -1;
  var lastPower = -999;
  var driverStatus = null;

  /* 散热模式原始码 → 中文名（依据 Linux 内核 alienware-wmi 官方文档 + AWCC WMI 枚举）
     USTT = User Selectable Thermal Table（用户可选散热表），m15 R7 使用这一组 */
  var POWER_NAMES = {
    0x00: '自定义 / 手动',
    0x96: '安静（旧款）',
    0x97: '平衡（旧款）',
    0x98: '性能（旧款）',
    0x99: '全速（旧款）',
    0xA0: '平衡',
    0xA1: '平衡-性能',
    0xA2: '冷却',
    0xA3: '安静',
    0xA4: '性能',
    0xA5: '低功耗',
    0xAB: 'G 模式'
  };
  function powerName(raw) { return POWER_NAMES[raw] ? L(POWER_NAMES[raw]) : (L('档位 ') + raw); }

  /* ---------- 状态渲染 ---------- */
  /* 启动与状态栏设置：从 telemetry 的 settings 字段回显 */
  function renderAppSettings(st) {
    if (!st) return;
    $('swLaunch').classList.toggle('on', !!st.launchAtLogin);
    $('swMinimized').classList.toggle('on', !!st.startMinimized);
    document.querySelectorAll('#tempSourceSwitch .lang-opt').forEach(function (b) {
      b.classList.toggle('active', parseInt(b.dataset.source, 10) === st.menuTempSource);
    });
  }

  function renderState(s) {
    if (!s) return;
    renderAppSettings(s.settings);

    // 连接状态
    var pill = $('connPill');
    pill.className = 'conn-pill ' + (s.connected ? 'on' : 'off');
    LZ($('connText'), s.connected
      ? (s.info.fans + ' FANS · ' + s.info.powers + ' MODES')
      : '驱动未连接');

    // 引擎主控（控制台内）：按钮与状态说明都取自真实运行状态 s.running
    var btn = $('btnEngine');
    LZ(btn, s.running ? '停用曲线' : '启用曲线');
    btn.classList.toggle('active', !!s.running);
    var center = $('engineCenter');
    if (center) center.classList.toggle('running', !!s.running);
    if ($('engineStateText')) {
      LZ($('engineStateText'), s.running
        ? '运行中 · 按当前曲线实时调节转速'
        : '未启用 · 风扇按开机 / BIOS 策略运转');
    }
    if ($('engineMemText')) LZ($('engineMemText'), '已记住上次选择');

    // 风扇
    var fans = s.fans || [];
    [0, 1].forEach(function (i) {
      var f = fans[i];
      $('g' + i + 'rpm').textContent = f ? f.rpm : '----';
      $('g' + i + 'boost').textContent = f ? f.boost + '%' : '--%';
      $('g' + i + 'max').textContent = f ? 'MAX ' + f.maxRpm : 'MAX ----';
      var pct = f && f.maxRpm ? Math.min(100, f.rpm / f.maxRpm * 100) : 0;
      $('g' + i + 'fill').style.width = pct + '%';
    });

    // 温度
    var temps = s.temps || [];
    [0, 1].forEach(function (i) {
      var el = $('temp' + i);
      var v = temps[i];
      $('t' + i).innerHTML = (v == null ? '--' : v) + '<small>°C</small>';
      el.classList.remove('cool', 'warm', 'hot');
      if (v != null) el.classList.add(v >= 75 ? 'hot' : v >= 60 ? 'warm' : 'cool');
    });
    var hottest = temps.length ? Math.max.apply(null, temps) : null;
    $('topTemp').textContent = (hottest == null ? '--' : hottest) + '°C';

    // 模式 / 目标
    IH($('tMode'), esc(powerName(s.currentPower)) +
      ' <small style="font-size:0.45em;opacity:.6;margin-left:4px">0x' +
      (s.currentPower >>> 0).toString(16).toUpperCase().padStart(2, '0') + '</small>');
    var target = fans.length && fans[0].boost != null ? Math.round(fans[0].boost / 100 * 4800) : null;
    $('tTarget').textContent = target == null ? '--' : target + ' RPM';

    // 多级档位高亮
    var boost = fans.length && fans[0].boost != null ? fans[0].boost : -1;
    document.querySelectorAll('.step').forEach(function (st) {
      st.classList.toggle('active', Math.abs(parseInt(st.dataset.pct, 10) - boost) <= 3);
    });
    $('levelCurrent').textContent = boost >= 0 ? boost + '%' : '--';
    /* 曲线接管时给多级调节明确视觉提示，解释「点档位会自动停曲线」的互斥行为 */
    var levelsPanel = document.querySelector('.levels');
    if (levelsPanel) levelsPanel.classList.toggle('auto-managed', !!s.running);

    // 曲线工作点
    $('cursorLine').setAttribute('x1', tx(hottest == null ? 30 : hottest));
    $('cursorLine').setAttribute('x2', tx(hottest == null ? 30 : hottest));
    $('cursorDot').setAttribute('cx', tx(hottest == null ? 30 : hottest));
    $('cursorDot').setAttribute('cy', ty(boost));
    IH($('curveReadout'), '最高温 <b>' + (hottest == null ? '--' : hottest) +
      '°C</b> → boost <b>' + (boost >= 0 ? boost + '%' : '--') + '</b>');

    // 预设列表（仅在名称变化时重建，避免打断交互）
    if (s.activeName !== activeName) {
      activeName = s.activeName;
      loadCurveFromPresets(s.presets, s.activeName);
    }
    renderPresets(s.presets || [], s.activeName);

    // 模式列表
    if (s.currentPower !== lastPower || $('modeList').children.length === 0) {
      lastPower = s.currentPower;
      renderModes(s.powers || [], s.currentPower);
    }

    // G 模式开关
    if (s.currentGMode !== lastGMode) {
      lastGMode = s.currentGMode;
      $('swGMode').classList.toggle('on', !!s.currentGMode);
    }
  }

  function tx(temp) {
    var t = Math.max(TEMP_MIN, Math.min(TEMP_MAX, temp));
    return (t - TEMP_MIN) / (TEMP_MAX - TEMP_MIN) * 1000;
  }
  function ty(boost) {
    var b = Math.max(0, Math.min(100, boost));
    return 300 - b / 100 * 300;
  }
  function invX(x) { return TEMP_MIN + x / 1000 * (TEMP_MAX - TEMP_MIN); }
  function invY(y) { return (300 - y) / 300 * 100; }

  /* ---------- 曲线 ---------- */
  function loadCurveFromPresets(presets, name) {
    var p = (presets || []).filter(function (x) { return x.name === name; })[0];
    if (!p || !p.points || p.points.length < 2) return;
    curvePoints = p.points.map(function (pair) { return { t: pair[0], b: pair[1] }; });
    drawCurve();
  }

  function drawCurve() {
    var pts = curvePoints.slice().sort(function (a, b) { return a.t - b.t; });
    if (!pts.length) return;
    var d = pts.map(function (p, i) {
      return (i ? 'L' : 'M') + tx(p.t).toFixed(1) + ' ' + ty(p.b).toFixed(1);
    }).join(' ');
    $('curvePath').setAttribute('d', d);
    $('curveArea').setAttribute('d', d + ' L1000 300 L0 300 Z');

    var group = $('pointGroup');
    group.innerHTML = '';
    pts.forEach(function (p) {
      var idx = curvePoints.indexOf(p);
      var c = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
      c.setAttribute('cx', tx(p.t));
      c.setAttribute('cy', ty(p.b));
      c.setAttribute('r', dragging === idx ? 9 : 7);
      c.setAttribute('class', 'ctrl-point' + (dragging === idx ? ' dragging' : ''));
      c.dataset.idx = idx;
      group.appendChild(c);
    });
  }

  function persistCurve() {
    return AF.call('setActivePreset', { preset: currentPresetDict() }).catch(toastErr);
  }

  function currentPresetDict() {
    return {
      name: activeName || '自定义',
      useCurve: true,
      fixedBoost: 0,
      powerRaw: -1,
      gmode: -1,
      points: curvePoints.map(function (p) { return [p.t, p.b]; })
    };
  }

  function initCurveEvents() {
    var stage = $('curveStage');

    function localPoint(e) {
      var r = stage.getBoundingClientRect();
      return { x: (e.clientX - r.left) / r.width * 1000, y: (e.clientY - r.top) / r.height * 300 };
    }

    stage.addEventListener('mousedown', function (e) {
      var c = e.target.closest ? e.target.closest('.ctrl-point') : null;
      if (!c) return;
      dragging = parseInt(c.dataset.idx, 10);
      drawCurve();
      e.preventDefault();
    });

    window.addEventListener('mousemove', function (e) {
      if (dragging < 0) return;
      var p = localPoint(e);
      curvePoints[dragging].t = Math.round(invX(p.x));
      curvePoints[dragging].b = Math.round(invY(p.y));
      drawCurve();
    });

    window.addEventListener('mouseup', function () {
      if (dragging < 0) return;
      dragging = -1;
      drawCurve();
      persistCurve();
      toast(L('曲线已更新，点「立即应用」生效'));
    });

    stage.addEventListener('dblclick', function (e) {
      var c = e.target.closest ? e.target.closest('.ctrl-point') : null;
      if (c) return;
      var p = localPoint(e);
      curvePoints.push({ t: Math.round(invX(p.x)), b: Math.round(invY(p.y)) });
      drawCurve();
      persistCurve();
      toast(L('曲线已更新，点「立即应用」生效'));
    });

    stage.addEventListener('contextmenu', function (e) {
      var c = e.target.closest ? e.target.closest('.ctrl-point') : null;
      if (!c) return;
      e.preventDefault();
      if (curvePoints.length <= 2) { toast(L('至少保留两个控制点')); return; }
      curvePoints.splice(parseInt(c.dataset.idx, 10), 1);
      drawCurve();
      persistCurve();
      toast(L('曲线已更新，点「立即应用」生效'));
    });
  }

  /* ---------- 渲染预设 ---------- */
  function renderPresets(presets, active) {
    var list = $('presetList');
    var sig = presets.map(function (p) { return p.name; }).join('|') + '::' + active;
    if (list.dataset.sig === sig) return;
    list.dataset.sig = sig;

    list.innerHTML = '';
    presets.forEach(function (p, i) {
      var card = document.createElement('div');
      card.className = 'preset-card' + (p.name === active ? ' active' : '');
      var mode = p.useCurve ? '温度曲线' : '固定 ' + p.fixedBoost + '%';
      IH(card,
        '<div class="preset-glyph">' + String(i + 1).padStart(2, '0') + '</div>' +
        '<div class="preset-info"><div class="preset-name">' + esc(p.name) + '</div>' +
        '<div class="preset-desc">' + mode + ' · ' + p.points.length + ' 个控制点</div></div>' +
        '<div class="preset-data">模式 <b>' + (p.powerRaw >= 0 ? '0x' + p.powerRaw.toString(16).toUpperCase() : '保持') +
        '</b><br>G 模式 <b>' + (p.gmode === 1 ? '开' : p.gmode === 0 ? '关' : '保持') + '</b></div>' +
        '<div class="preset-btns">' +
        '<button class="btn-sm" data-act="load" data-name="' + esc(p.name) + '">载入</button>' +
        '<button class="btn-sm danger" data-act="del" data-name="' + esc(p.name) + '">删除</button>' +
        '</div>');
      list.appendChild(card);
    });
  }

  function renderModes(powers, current) {
    var box = $('modeList');
    box.innerHTML = '';
    powers.forEach(function (raw) {
      var row = document.createElement('div');
      row.className = 'mode-row' + (raw === current ? ' active' : '');
      row.dataset.raw = raw;
      var hex = '0x' + raw.toString(16).toUpperCase().padStart(2, '0');
      IH(row, '<span class="mode-hex">' + hex + '</span>' +
        '<span class="mode-name">' + esc(powerName(raw)) + '</span>' +
        '<span class="mode-note">' + (raw === current ? L('● 当前') : '') + '</span>');
      row.addEventListener('click', function () {
        AF.call('setPower', { raw: raw })
          .then(function () {
            var running = AF.lastState && AF.lastState.running;
            toast('已切到 ' + hex + (running
              ? L('，曲线运行中，下一拍补发 boost')
              : L('，曲线未运行，请到控制台设定档位')));
          })
          .catch(toastErr);
      });
      box.appendChild(row);
    });
    if (!powers.length) IH(box, '<div class="adv-desc">读取模式表失败。</div>');
  }

  function esc(s) {
    return String(s).replace(/[&<>"]/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c];
    });
  }

  /* ---------- 导航 ---------- */
  function initNav() {
    document.querySelectorAll('.nav-item').forEach(function (btn) {
      btn.addEventListener('click', function () {
        document.querySelectorAll('.nav-item').forEach(function (b) { b.classList.remove('active'); });
        document.querySelectorAll('.view').forEach(function (v) { v.classList.remove('active'); });
        btn.classList.add('active');
        $('view-' + btn.dataset.view).classList.add('active');
        $('main').scrollTop = 0;
      });
    });
  }

  /* ---------- 事件绑定 ---------- */
  function initEvents() {
    $('btnEngine').addEventListener('click', function () {
      var running = AF.lastState && AF.lastState.running;
      AF.call('engine', { op: running ? 'stop' : 'start' })
        .then(function () { toast(L(running ? '曲线已停用' : '曲线已启用')); })
        .catch(toastErr);
    });

    document.querySelectorAll('.step').forEach(function (st) {
      st.addEventListener('click', function () {
        var pct = parseInt(st.dataset.pct, 10);
        AF.call('setBoost', { index: -1, pct: pct })
          .then(function (r) {
            /* 原生层会先停曲线再下发，避免下一拍覆盖；这里给出明确反馈 */
            if (r && r.data && r.data.curveStopped) {
              toast(L('已切到手动档位，曲线已停用'));
            } else {
              toast(L('已下发 ') + pct + L('% 到两个风扇'));
            }
          })
          .catch(toastErr);
      });
    });

    $('btnCurveApply').addEventListener('click', function () {
      AF.call('engine', { op: 'apply' }).then(function () { toast(L('曲线已应用')); }).catch(toastErr);
    });

    $('btnPresetFromCurve').addEventListener('click', function () {
      showPresetPicker(function (name) {
        AF.call('activatePreset', { name: name }).then(function () { toast(L('已载入 ') + name); }).catch(toastErr);
      });
    });

    $('btnPresetReveal').addEventListener('click', function () {
      AF.call('revealConfig').catch(toastErr);
    });

    $('btnPresetReset').addEventListener('click', function () {
      if (!window.confirm(L('恢复内置预设？当前自定义预设会被覆盖。'))) return;
      AF.call('resetPresets').then(function () { toast(L('已恢复内置预设')); }).catch(toastErr);
    });

    $('presetList').addEventListener('click', function (e) {
      var b = e.target.closest('button[data-act]');
      if (!b) return;
      var name = b.dataset.name;
      if (b.dataset.act === 'load') {
        AF.call('activatePreset', { name: name }).then(function () { toast(L('已载入 ') + name); }).catch(toastErr);
      } else {
        if (!window.confirm(L('删除预设「') + name + L('」？'))) return;
        AF.call('deletePreset', { name: name }).then(function () { toast(L('已删除')); }).catch(toastErr);
      }
    });

    $('swGMode').addEventListener('click', function () {
      var on = !$('swGMode').classList.contains('on');
      AF.call('setGMode', { on: on ? 1 : 0 })
        .then(function () { toast(L(on ? 'G 模式已开启' : 'G 模式已关闭')); })
        .catch(toastErr);
    });

    $('btnEngineStop').addEventListener('click', function () {
      AF.call('engine', { op: 'stop' }).then(function () { toast(L('曲线已停用')); }).catch(toastErr);
    });

    $('btnRestoreBios').addEventListener('click', function () {
      if (!window.confirm(L('恢复 BIOS 控制？boost 将归 0。'))) return;
      AF.call('restoreBios').then(function () { toast(L('已交还 BIOS 控制')); }).catch(toastErr);
    });

    $('rawRun').addEventListener('click', function () {
      var args = {
        com: parseNum($('rawCom').value), sub: parseNum($('rawSub').value),
        a1: parseNum($('rawA1').value), a2: parseNum($('rawA2').value)
      };
      AF.call('raw', args).then(function (r) {
        $('rawResult').innerHTML = 'WMAX(0x' + args.com.toString(16) + ', 0x' + args.sub.toString(16) +
          ', 0x' + args.a1.toString(16) + ', 0x' + args.a2.toString(16) + ') = <b>' +
          r.data.value + ' (' + r.data.hex + ')</b>';
      }).catch(toastErr);
    });

    $('paramInterval').addEventListener('input', function () {
      var v = parseFloat(this.value);
      $('outInterval').textContent = v.toFixed(1) + 's';
      $('annotInterval').textContent = v.toFixed(1);
      AF.call('setEngineParams', { interval: v }).catch(function () {});
    });
    $('paramStep').addEventListener('input', function () {
      var v = parseInt(this.value, 10);
      $('outStep').textContent = v + '%';
      $('annotStep').textContent = v;
      AF.call('setEngineParams', { maxStep: v }).catch(function () {});
    });

    $('btnRecheck').addEventListener('click', function () { loadDriverStatus(true); });

    /* 必须限定 #langSwitch：温度来源按钮也用 .lang-opt，误绑会在切换温度来源时把语言强切回中文 */
    document.querySelectorAll('#langSwitch .lang-opt').forEach(function (b) {
      b.addEventListener('click', function () {
        var lang = b.dataset.lang === 'en' ? 'en' : 'zh';
        AF.call('setLanguage', { language: lang })
          .then(function () { applyLanguage(lang); })
          .catch(toastErr);
      });
    });

    $('swLaunch').addEventListener('click', function () {
      var on = !$('swLaunch').classList.contains('on');
      AF.call('setLaunchAtLogin', { on: on })
        .then(function (r) {
          renderAppSettings(r.data);
          toast(L(on ? '已开启开机自启' : '已关闭开机自启'));
        }).catch(toastErr);
    });

    $('swMinimized').addEventListener('click', function () {
      var on = !$('swMinimized').classList.contains('on');
      AF.call('setStartMinimized', { on: on })
        .then(function (r) {
          renderAppSettings(r.data);
          toast(L(on ? '已开启启动最小化' : '已关闭启动最小化'));
        }).catch(toastErr);
    });

    document.querySelectorAll('#tempSourceSwitch .lang-opt').forEach(function (b) {
      b.addEventListener('click', function () {
        var src = parseInt(b.dataset.source, 10);
        AF.call('setMenuTempSource', { source: src })
          .then(function (r) {
            renderAppSettings(r.data);
            toast(L('状态栏温度已切换'));
          }).catch(toastErr);
      });
    });

    document.addEventListener('afstatus', function (e) {
      setStatus(e.detail.text, e.detail.warn);
    });
  }

  function parseNum(s) {
    var v = parseInt(s, 0);
    return isNaN(v) ? 0 : v;
  }

  function showPresetPicker(cb) {
    var s = AF.lastState || {};
    var names = (s.presets || []).map(function (p) { return p.name; });
    var name = window.prompt(L('输入预设名称：\n已有：') + names.join(' / '), L(s.activeName || '自定义'));
    if (name) cb(name);
  }

  /* ---------- 驱动状态 ---------- */
  function setFlag(id, on) {
    var el = $(id);
    LZ(el, on ? '是' : '否');
    el.classList.toggle('ok', !!on);
    el.classList.toggle('warn', !on);
  }

  function renderDriverStatus(d) {
    if (!d) return;
    var good = !!d.compatible;
    var badge = $('bindBadge');
    LZ(badge, good ? '版本匹配' : '版本不匹配');
    badge.className = 'ver-badge ' + (good ? 'ok' : 'warn');

    LZ($('setAppVer'), 'v' + (d.appVersion || '--'));
    LZ($('setKextVer'), d.kextVersion ? 'v' + d.kextVersion : '—');
    LZ($('setReqVer'), 'v' + (d.requiredVersion || '--'));
    LZ($('footKextVer'), d.kextVersion ? 'v' + d.kextVersion : 'v?');
    LZ($('footAppVer'), 'v' + (d.appVersion || '?'));

    var hint = '驱动已就绪，可以正常使用。';
    if (!d.installed) hint = '驱动未安装：请先安装随附的 AlienFan.kext。';
    else if (!d.loaded) hint = '驱动已安装但内核未加载：重启或批准系统扩展后重试。';
    else if (!d.compatible) hint = '驱动版本过低或不兼容：请使用与本软件一同发布的驱动。';
    LZ($('bindHint'), hint);
    $('bindHint').className = 'set-hint ' + (good ? 'ok' : 'warn');

    setFlag('stInstalled', d.installed);
    setFlag('stLoaded', d.loaded);
    setFlag('stReady', d.readyInstances > 0);
    setFlag('stClient', d.userClient);

    LZ($('stRole'), d.role || '未知');
    LZ($('stDevice'), d.device || '—');
    LZ($('stSysid'), d.systemId >= 0 ? '0x' + (d.systemId >>> 0).toString(16).toUpperCase() : '—');
    LZ($('stCounts'), d.fanCount + ' / ' + d.sensorCount + ' / ' + d.powerCount);
    LZ($('stLive'), (d.boost >= 0 ? d.boost + '%' : '—') + ' / 0x' +
       ((d.mode >>> 0) & 0xff).toString(16).toUpperCase().padStart(2, '0'));
    LZ($('stDiag'), d.diag || '—');
    LZ($('repoUrl'), REPO_URL);
  }

  function loadDriverStatus(notify) {
    return AF.call('getDriverStatus').then(function (r) {
      driverStatus = r.data || {};
      renderDriverStatus(driverStatus);
      if (notify) toast(L('重新检测完成'));
    }).catch(function (e) {
      LZ($('bindHint'), '读取驱动诊断信息失败。');
      $('bindHint').className = 'set-hint warn';
      LZ($('bindBadge'), '失败');
      $('bindBadge').className = 'ver-badge warn';
      if (notify) toastErr(e);
    });
  }

  function syncLangButtons(lang) {
    document.querySelectorAll('#langSwitch .lang-opt').forEach(function (b) {
      b.classList.toggle('active', b.dataset.lang === lang);
    });
  }

  function applyLanguage(lang) {
    window.I18N.set(lang);
    syncLangButtons(lang);
    paintStatus();
    if (driverStatus) renderDriverStatus(driverStatus);
  }

  /* ---------- 状态条与提示 ---------- */
  var statusRenderer = null;
  function paintStatus() {
    if (!statusRenderer) return;
    var r = statusRenderer();
    $('statusText').textContent = r.text;
    $('statusbar').classList.toggle('warn', !!r.warn);
  }
  function setStatus(text, warn) {
    statusRenderer = function () { return { text: L(text), warn: !!warn }; };
    paintStatus();
  }
  function statusFromState(s) {
    statusRenderer = function () {
      return {
        text: s.connected
          ? L('已连接 · ') + s.info.fans + L(' 风扇 · ') + s.info.sensors +
            L(' 传感器 · ') + s.info.powers + L(' 档模式')
          : L(s.error || '驱动未连接'),
        warn: !s.connected
      };
    };
    paintStatus();
  }

  var toastTimer = null;
  function toast(text) {
    var el = $('toast');
    el.textContent = text;
    el.classList.add('show');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { el.classList.remove('show'); }, 2200);
  }
  function toastErr(e) { toast(L('错误：') + L(e.message || '调用失败')); }

  /* ---------- 入场序列 ---------- */
  function revealAll() {
    var groups = document.querySelectorAll('.reveal');
    groups.forEach(function (el, i) {
      setTimeout(function () { el.classList.add('in'); }, 60 * (i % 6));
    });
  }

  /* ---------- 启动 ---------- */
  function init() {
    initNav();
    initCurveEvents();
    initEvents();
    AF.onState(renderState);
    AF.whenReady(function () {
      var s = AF.lastState;
      var lang = (s && s.language === 'en') ? 'en' : 'zh';
      window.I18N.set(lang);
      syncLangButtons(lang);
      loadDriverStatus(false);
      if (s) {
        statusFromState(s);
        $('paramInterval').value = s.interval;
        $('paramStep').value = s.maxStep;
        $('outInterval').textContent = Number(s.interval).toFixed(1) + 's';
        $('outStep').textContent = s.maxStep + '%';
        $('annotInterval').textContent = Number(s.interval).toFixed(1);
        $('annotStep').textContent = s.maxStep;
        loadCurveFromPresets(s.presets, s.activeName);
      }
      revealAll();
    });
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
  else init();
})();
