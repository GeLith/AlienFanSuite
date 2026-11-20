#import "AppDelegate.h"
#import <WebKit/WebKit.h>
#import "curve.h"
#import "AFBridge.h"

#define W 1220.0
#define H 820.0

/* 菜单栏与窗口标题跟随 Web UI 的语言设置 */
static NSString *AFLS(NSString *zh, NSString *en)
{
    return [[AFBridge currentLanguage] isEqualToString:@"en"] ? en : zh;
}

@interface AppDelegate ()
@property (nonatomic, strong) WKWebView *webView;
@property (nonatomic, strong) AFBridge *bridge;
@property (nonatomic, strong) AFCurveEngine *engine;
@property (nonatomic, strong) NSStatusItem *statusItem;
@property (nonatomic, strong) NSTimer *uiTimer;
@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
    _engine = [AFCurveEngine shared];
    [_engine connect];

    _bridge = [[AFBridge alloc] initWithEngine:_engine];
    __weak AppDelegate *weakSelf = self;
    _bridge.onPresetsChanged = ^{ [weakSelf rebuildStatusMenu]; };
    _bridge.onEngineChanged = ^(BOOL running) {
        [weakSelf.bridge pushTelemetry];
    };
    _bridge.onLanguageChanged = ^(NSString *language) {
        [weakSelf rebuildStatusMenu];
        weakSelf.window.title = AFLS(@"FAN CONTROL — Alienware 风扇控制台",
                                     @"FAN CONTROL — Alienware Fan Console");
    };
    _bridge.onMenuTempSourceChanged = ^{
        [weakSelf updateStatusBarTitle];
        [weakSelf rebuildStatusMenu];
    };

    [self buildWindow];
    [self buildStatusBar];

    /* 依据记忆恢复曲线引擎：重启后按钮显示与实际运行严格一致。
       上次启用→自动 start（按钮显示“停用曲线”）；上次停用→保持 stop，
       不再出现“按钮写着启用曲线、实际风扇却是曲线在驱动”的状态漂移。 */
    if (_engine.connected && [AFCurveEngine curveEnabledRemembered]) {
        [_engine start];
    }

    /* 登录自启带 --background，或用户在设置里开启“启动最小化”时，仅驻留状态栏 */
    BOOL background = [[[NSProcessInfo processInfo] arguments] containsObject:@"--background"];
    if (background || [AFBridge startMinimized]) {
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];  /* 隐藏 Dock 图标，纯菜单栏 */
        [_window orderOut:nil];
    } else {
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [_window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
    }

    [self updateStatusBarTitle];

    if (!_engine.connected) {
        [_bridge pushStatus:[NSString stringWithFormat:@"%@：%@",
                             AFLS(@"驱动未连接", @"Driver not connected"),
                             _engine.lastError ?: AFLS(@"未知错误", @"Unknown error")] warn:YES];
    }

    _uiTimer = [NSTimer scheduledTimerWithTimeInterval:1.0
                                                target:self
                                              selector:@selector(pushTick:)
                                              userInfo:nil repeats:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender { return NO; }

- (BOOL)windowShouldClose:(NSWindow *)window
{
    /* 菜单栏常驻工具：关闭按钮视为隐藏，窗口对象保留，避免悬空指针 */
    [window orderOut:nil];
    return NO;
}

- (void)applicationWillTerminate:(NSNotification *)notification
{
    [_engine savePresets:_bridge.presets activeName:_bridge.activeName];
}

#pragma mark - 窗口

- (void)buildWindow
{
    NSRect frame = NSMakeRect(80, 60, W, H);
    _window = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                    NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                            backing:NSBackingStoreBuffered defer:NO];
    _window.title = AFLS(@"FAN CONTROL — Alienware 风扇控制台",
                         @"FAN CONTROL — Alienware Fan Console");
    _window.delegate = self;
    _window.titlebarAppearsTransparent = YES;
    _window.titleVisibility = NSWindowTitleHidden;
    _window.backgroundColor = [NSColor colorWithCalibratedRed:0.043 green:0.051 blue:0.063 alpha:1];
    _window.minSize = NSMakeSize(1000, 700);
    /* ARC 下 strong 持有的 NSWindow 必须关掉自动释放：否则点关闭按钮后
       AppKit 会再 release 一次，_window 变野指针，重新显示时必崩 */
    _window.releasedWhenClosed = NO;
    [_window center];

    /* WKWebView 配置：允许访问 app 内部 web 目录 */
    WKWebViewConfiguration *cfg = [WKWebViewConfiguration new];
    WKUserContentController *uc = [WKUserContentController new];
    [uc addScriptMessageHandler:_bridge name:@"af"];
    cfg.userContentController = uc;

    _webView = [[WKWebView alloc] initWithFrame:_window.contentView.bounds configuration:cfg];
    _webView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    _webView.navigationDelegate = self;
    _webView.allowsMagnification = NO;
    [_window.contentView addSubview:_webView];
    _bridge.webView = _webView;

    NSURL *webDir = [[NSBundle mainBundle] URLForResource:@"web" withExtension:nil];
    NSURL *index = [webDir URLByAppendingPathComponent:@"index.html"];
    if ([[NSFileManager defaultManager] fileExistsAtPath:index.path]) {
        [_webView loadFileURL:index allowingReadAccessToURL:webDir];
    } else {
        [_webView loadHTMLString:@"<h1 style='color:#fff;font-family:monospace'>web 资源缺失</h1>" baseURL:nil];
    }
}

- (void)webView:(WKWebView *)webView didFinishNavigation:(WKNavigation *)navigation
{
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.35 * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
                       [self.bridge pushTelemetry];
                   });
}

- (void)webView:(WKWebView *)webView didFailNavigation:(WKNavigation *)navigation withError:(NSError *)error
{
    NSLog(@"[FanControl] web 加载失败: %@", error);
}

- (void)pushTick:(NSTimer *)timer
{
    [_bridge pushTelemetry];
    [self updateStatusBarTitle];
}

/* 按用户选择的来源刷新菜单栏温度：0 最高温 / 1 CPU / 2 显卡 */
- (void)updateStatusBarTitle
{
    int temps[AF_MAX_SENSORS] = {0};
    int n = [_engine readTemps:temps max:AF_MAX_SENSORS];
    NSInteger src = [AFBridge menuTempSource];
    int value = -1000;
    if (src == 1 && n >= 1) {
        value = temps[0];                       /* CPU = 传感器 0 */
    } else if (src == 2 && n >= 2) {
        value = temps[1];                       /* 显卡 = 传感器 1 */
    } else {
        for (int i = 0; i < n; i++) if (temps[i] > value) value = temps[i];
    }
    _statusItem.button.title = value > -900 ? [NSString stringWithFormat:@"%d°", value] : @"◇°";

    /* 悬浮提示显示两个传感器的读数，便于对照 */
    if (n >= 2) {
        _statusItem.button.toolTip = [NSString stringWithFormat:
            AFLS(@"CPU %d° · 显卡 %d°", @"CPU %d° · GPU %d°"), temps[0], temps[1]];
    } else if (n == 1) {
        _statusItem.button.toolTip = [NSString stringWithFormat:
            AFLS(@"CPU %d°", @"CPU %d°"), temps[0]];
    } else {
        _statusItem.button.toolTip = @"FanControl";
    }
}

#pragma mark - 菜单栏

- (void)buildStatusBar
{
    _statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    _statusItem.button.title = @"◇°";
    _statusItem.button.toolTip = @"FanControl";
    [self rebuildStatusMenu];
}

- (void)rebuildStatusMenu
{
    NSMenu *menu = [NSMenu new];
    [menu addItemWithTitle:AFLS(@"显示主窗口", @"Show Window") action:@selector(showWindow:) keyEquivalent:@""].target = self;
    [menu addItemWithTitle:AFLS(@"驱动状态 / 设置", @"Driver Status / Settings") action:@selector(showSettings:) keyEquivalent:@""].target = self;
    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:AFLS(@"启用曲线", @"Start Curve") action:@selector(startEngine:) keyEquivalent:@""].target = self;
    [menu addItemWithTitle:AFLS(@"停用曲线", @"Stop Curve") action:@selector(stopEngine:) keyEquivalent:@""].target = self;
    [menu addItemWithTitle:AFLS(@"恢复 BIOS 控制", @"Restore BIOS Control") action:@selector(restoreBios:) keyEquivalent:@""].target = self;
    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *head = [[NSMenuItem alloc] initWithTitle:AFLS(@"快速切换预设", @"Quick Profiles") action:nil keyEquivalent:@""];
    NSMenu *pm = [NSMenu new];
    for (AFPreset *p in _bridge.presets) {
        NSMenuItem *it = [[NSMenuItem alloc] initWithTitle:p.name action:@selector(quickPreset:) keyEquivalent:@""];
        it.representedObject = p.name;
        it.target = self;
        [pm addItem:it];
    }
    head.submenu = pm;
    [menu addItem:head];

    [menu addItem:[NSMenuItem separatorItem]];

    /* 状态栏温度来源：快速切换，与设置页共用偏好 */
    NSMenuItem *tempHead = [[NSMenuItem alloc] initWithTitle:AFLS(@"状态栏温度", @"Menu Bar Temp")
                                                      action:nil keyEquivalent:@""];
    NSMenu *tempMenu = [NSMenu new];
    NSArray *tempTitles = @[AFLS(@"最高温", @"Hottest"), AFLS(@"处理器", @"CPU"), AFLS(@"显卡", @"GPU")];
    NSInteger current = [AFBridge menuTempSource];
    for (NSInteger i = 0; i < 3; i++) {
        NSMenuItem *it = [[NSMenuItem alloc] initWithTitle:tempTitles[i]
                                                    action:@selector(setMenuTempSource:) keyEquivalent:@""];
        it.tag = i;
        it.target = self;
        it.state = (i == current) ? NSOnState : NSOffState;
        [tempMenu addItem:it];
    }
    tempHead.submenu = tempMenu;
    [menu addItem:tempHead];

    [menu addItemWithTitle:AFLS(@"退出", @"Quit") action:@selector(quit:) keyEquivalent:@"q"].target = self;
    _statusItem.menu = menu;
}

- (void)setMenuTempSource:(NSMenuItem *)sender
{
    [AFBridge setMenuTempSource:sender.tag];
    [self updateStatusBarTitle];
    [self rebuildStatusMenu];
}

- (void)quickPreset:(NSMenuItem *)sender
{
    NSString *name = sender.representedObject;
    [_bridge activatePresetNamed:name];
    /* 与 Web UI 的载入路径保持一致：曲线预设自动接管引擎，固定档位直接下发 */
    AFPreset *p = nil;
    for (AFPreset *item in _bridge.presets)
        if ([item.name isEqualToString:name]) { p = item; break; }
    if (p && p.useCurve) {
        if (!_engine.running) [_engine start];
        else [_engine applyOnce];
    } else if (p) {
        [_engine setFixedBoost:p.fixedBoost];
        if (_engine.running) [_engine stop];
    }
    [_bridge pushTelemetry];
}
- (void)showWindow:(id)sender
{
    /* 恢复 Dock 图标与激活态，否则 Accessory 模式下窗口无法成为 key window */
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    if (!_window) [self buildWindow];   /* 兜底：异常路径释放后重建 */
    [_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}
- (void)showSettings:(id)sender
{
    [self showWindow:nil];
    [_webView evaluateJavaScript:
        @"var b=document.querySelector('[data-view=settings]'); if(b){b.click();}"
     completionHandler:nil];
    [_bridge pushTelemetry];
}
- (void)startEngine:(id)sender
{
    if (!_engine.running) [_engine start];
    [_bridge pushTelemetry];
}
- (void)stopEngine:(id)sender
{
    [_engine stop];
    [_bridge pushTelemetry];
}
- (void)restoreBios:(id)sender
{
    [_engine stop];
    [_engine setFixedBoost:0];
    [_bridge pushTelemetry];
}
- (void)quit:(id)sender { [NSApp terminate:nil]; }

- (void)dealloc
{
    [_uiTimer invalidate];
}

@end
