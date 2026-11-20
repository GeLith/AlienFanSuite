#import "AFBridge.h"
#import "curve.h"
#import <AppKit/AppKit.h>

/* 本版本 App 要求的驱动版本。主版本必须一致，次版本不能低于此值。 */
static NSString * const AFRequiredDriverVersion = @"2.1.0";
static NSString * const AFLanguageKey = @"AlienFanLanguage";
static NSString * const AFStartMinimizedKey = @"AlienFanStartMinimized";
static NSString * const AFMenuTempSourceKey = @"AlienFanMenuTempSource";
static NSString * const AFLaunchAgentLabel = @"com.dongdong.FanControl";
static NSString * const AFLaunchAgentFile = @"com.dongdong.FanControl.plist";

@implementation AFBridge

+ (NSString *)currentLanguage
{
    NSString *saved = [[NSUserDefaults standardUserDefaults] stringForKey:AFLanguageKey];
    if ([saved isEqualToString:@"en"] || [saved isEqualToString:@"zh"]) return saved;
    NSString *preferred = [[NSLocale preferredLanguages] firstObject] ?: @"";
    return [preferred hasPrefix:@"zh"] ? @"zh" : @"en";
}

#pragma mark - 启动与状态栏设置

+ (BOOL)startMinimized
{
    return [[NSUserDefaults standardUserDefaults] boolForKey:AFStartMinimizedKey];
}

+ (void)setStartMinimized:(BOOL)on
{
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    [ud setBool:on forKey:AFStartMinimizedKey];
    [ud synchronize];
}

+ (NSInteger)menuTempSource
{
    NSInteger v = [[NSUserDefaults standardUserDefaults] integerForKey:AFMenuTempSourceKey];
    return (v >= 0 && v <= 2) ? v : 0;   /* 0=最高温 1=CPU 2=显卡，默认最高温 */
}

+ (void)setMenuTempSource:(NSInteger)source
{
    if (source < 0 || source > 2) source = 0;
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    [ud setInteger:source forKey:AFMenuTempSourceKey];
    [ud synchronize];
}

+ (NSString *)launchAgentPath
{
    return [NSHomeDirectory() stringByAppendingPathComponent:
            [@"Library/LaunchAgents" stringByAppendingPathComponent:AFLaunchAgentFile]];
}

+ (BOOL)isLaunchAtLogin
{
    return [[NSFileManager defaultManager] fileExistsAtPath:[self launchAgentPath]];
}

+ (BOOL)setLaunchAtLogin:(BOOL)on
{
    NSString *path = [self launchAgentPath];
    NSFileManager *fm = [NSFileManager defaultManager];
    if (!on) {
        if ([fm fileExistsAtPath:path]) return [fm removeItemAtPath:path error:NULL];
        return YES;
    }

    NSString *exe = [[NSBundle mainBundle] executablePath] ?: @"";
    if (!exe.length) return NO;
    /* --background 让登录启动时不弹主窗口，仅驻留状态栏 */
    NSString *xml = [NSString stringWithFormat:
        @"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        @"<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        @"<plist version=\"1.0\"><dict>\n"
        @"  <key>Label</key><string>%@</string>\n"
        @"  <key>ProgramArguments</key><array>\n"
        @"    <string>%@</string>\n"
        @"    <string>--background</string>\n"
        @"  </array>\n"
        @"  <key>RunAtLoad</key><true/>\n"
        @"  <key>KeepAlive</key><false/>\n"
        @"  <key>ProcessType</key><string>Background</string>\n"
        @"</dict></plist>\n", AFLaunchAgentLabel, exe];

    NSString *dir = [path stringByDeletingLastPathComponent];
    [fm createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:NULL];
    return [xml writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:NULL];
}

+ (NSDictionary *)appSettings
{
    return @{
        @"launchAtLogin":   @([self isLaunchAtLogin]),
        @"startMinimized":  @([self startMinimized]),
        @"menuTempSource":  @([self menuTempSource])
    };
}

#pragma mark - 驱动绑定与状态

/* 解析 "2.1.0" → @[@2,@1,@0]，用于版本兼容判断 */
static NSArray<NSNumber *> *AFParseVersion(NSString *s)
{
    NSMutableArray *out = [NSMutableArray array];
    for (NSString *part in [s componentsSeparatedByString:@"."]) {
        NSInteger v = [part integerValue];
        [out addObject:@(MAX(0, v))];
    }
    while (out.count < 3) [out addObject:@0];
    return [out subarrayWithRange:NSMakeRange(0, 3)];
}

static BOOL AFVersionCompatible(NSString *installed, NSString *required)
{
    if (installed.length == 0) return NO;
    NSArray<NSNumber *> *a = AFParseVersion(installed);
    NSArray<NSNumber *> *b = AFParseVersion(required);
    if (a[0].integerValue != b[0].integerValue) return NO;   /* 主版本必须一致 */
    if (a[1].integerValue != b[1].integerValue) return a[1].integerValue > b[1].integerValue;
    return a[2].integerValue >= b[2].integerValue;
}

- (NSString *)installedKextVersionFromDisk
{
    NSArray *paths = @[
        @"/Library/Extensions/AlienFan.kext/Contents/Info.plist",
        @"/System/Library/Extensions/AlienFan.kext/Contents/Info.plist"
    ];
    for (NSString *path in paths) {
        NSDictionary *info = [NSDictionary dictionaryWithContentsOfFile:path];
        NSString *v = info[@"CFBundleVersion"] ?: info[@"CFBundleShortVersionString"];
        if ([v isKindOfClass:[NSString class]] && v.length) return v;
    }
    return @"";
}

- (NSDictionary *)driverStatus
{
    AFDriverScan scan;
    int rc = af_scan(&scan);
    if (rc < 0) memset(&scan, 0, sizeof(scan));

    NSFileManager *fm = [NSFileManager defaultManager];
    BOOL installed = [fm fileExistsAtPath:@"/Library/Extensions/AlienFan.kext"];
    BOOL cliInstalled = [fm fileExistsAtPath:@"/usr/local/bin/alienfanctl"];

    NSString *kextVer = @"";
    if (scan.bundleVersion[0]) kextVer = [NSString stringWithUTF8String:scan.bundleVersion];
    if (!kextVer.length) kextVer = [self installedKextVersionFromDisk];

    AFFan fans[AF_MAX_FANS] = {0};
    int nf = [_engine readFans:fans max:AF_MAX_FANS];
    AFInfo inf = _engine.info;

    NSString *appVer = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleShortVersionString"] ?: @"?";
    BOOL compatible = AFVersionCompatible(kextVer, AFRequiredDriverVersion);

    return @{
        @"installed":       @(installed),
        @"cliInstalled":    @(cliInstalled),
        @"loaded":          @(scan.instances > 0),
        @"instances":       @(scan.instances),
        @"readyInstances":  @(scan.readyInstances),
        @"userClient":      @(_engine.connected),
        @"role":            scan.role[0] ? [NSString stringWithUTF8String:scan.role] : @"",
        @"diag":            scan.diag[0] ? [NSString stringWithUTF8String:scan.diag] : @"",
        @"device":          scan.device[0] ? [NSString stringWithUTF8String:scan.device] : @"",
        @"kextVersion":     kextVer,
        @"requiredVersion": AFRequiredDriverVersion,
        @"appVersion":      appVer,
        @"compatible":      @(compatible),
        @"fanCount":        @(scan.fanCount >= 0 ? scan.fanCount : inf.fans),
        @"sensorCount":     @(scan.sensorCount >= 0 ? scan.sensorCount : inf.sensors),
        @"powerCount":      @(scan.powerCount >= 0 ? scan.powerCount : inf.powers),
        @"systemId":        @(scan.systemId),
        @"boost":           @(nf > 0 ? fans[0].boost : -1),
        @"mode":            @([_engine currentPowerRaw]),
        @"lastError":       _engine.lastError ?: @""
    };
}

- (instancetype)initWithEngine:(AFCurveEngine *)engine
{
    if ((self = [super init])) {
        _engine = engine;
        NSArray *loaded = [engine loadPresets];
        _presets = [loaded mutableCopy] ?: [NSMutableArray array];
        if (_presets.count == 0) _presets = [[self builtinPresets] mutableCopy];
        _activeName = [engine loadActiveName] ?: @"性能";
        if (![self presetNamed:_activeName]) _activeName = ((AFPreset *)_presets[0]).name;
        engine.activePreset = [self presetNamed:_activeName];

        /* 一次性升级：把内置三套的旧稀疏曲线（3–4 点）换成 12 点密集版。
           只替换曲线点，保留用户对该预设做的模式 / G 模式 / boost 等设置；
           用户自定义的其它预设完全不动。用标记确保只升一次，之后用户改点不会被重置。 */
        NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
        if (![ud boolForKey:@"AlienFanCurveDenseV2"]) {
            NSDictionary *builtins = @{
                @"静音": [AFPreset defaultQuiet],
                @"均衡": [AFPreset defaultBalanced],
                @"性能": [AFPreset defaultPerformance]
            };
            BOOL changed = NO;
            for (AFPreset *p in _presets) {
                AFPreset *def = builtins[p.name];
                if (def && p.points.count < def.points.count) {
                    p.points = def.points;
                    changed = YES;
                }
            }
            [ud setBool:YES forKey:@"AlienFanCurveDenseV2"];
            if (changed) [_engine savePresets:_presets activeName:_activeName];
        }
    }
    return self;
}

#pragma mark - 预设

- (AFPreset *)presetNamed:(NSString *)name
{
    for (AFPreset *p in _presets) if ([p.name isEqualToString:name]) return p;
    return nil;
}

- (NSArray<AFPreset *> *)builtinPresets
{
    return @[[AFPreset defaultQuiet], [AFPreset defaultBalanced], [AFPreset defaultPerformance]];
}

- (void)resetToBuiltins
{
    _presets = [[self builtinPresets] mutableCopy];
    _activeName = @"性能";
    _engine.activePreset = [self presetNamed:_activeName];
    [self persistPresets];
}

- (void)persistPresets
{
    [_engine savePresets:_presets activeName:_activeName];
    if (_onPresetsChanged) _onPresetsChanged();
}

- (void)activatePresetNamed:(NSString *)name
{
    AFPreset *p = [self presetNamed:name];
    if (!p) return;
    _activeName = name;
    _engine.activePreset = p;
    [self persistPresets];
}

#pragma mark - JS 消息入口

- (void)userContentController:(WKUserContentController *)uc
      didReceiveScriptMessage:(WKScriptMessage *)message
{
    if (![message.body isKindOfClass:[NSDictionary class]]) return;
    NSDictionary *body = message.body;
    id ident = body[@"id"] ?: @0;
    NSString *cmd = body[@"cmd"] ?: @"";
    NSDictionary *args = [body[@"args"] isKindOfClass:[NSDictionary class]] ? body[@"args"] : @{};

    NSMutableDictionary *reply = [NSMutableDictionary dictionaryWithObject:@YES forKey:@"ok"];
    id data = nil;

    if ([cmd isEqualToString:@"getState"]) {
        data = [self stateSnapshot];
    } else if ([cmd isEqualToString:@"getDriverStatus"]) {
        data = [self driverStatus];
    } else if ([cmd isEqualToString:@"setLanguage"]) {
        NSString *lang = [args[@"language"] isEqualToString:@"en"] ? @"en" : @"zh";
        [[NSUserDefaults standardUserDefaults] setObject:lang forKey:AFLanguageKey];
        [[NSUserDefaults standardUserDefaults] synchronize];
        data = @{@"language": lang};
        if (_onLanguageChanged) _onLanguageChanged(lang);
    } else if ([cmd isEqualToString:@"getAppSettings"]) {
        data = [AFBridge appSettings];
    } else if ([cmd isEqualToString:@"setLaunchAtLogin"]) {
        BOOL ok = [AFBridge setLaunchAtLogin:[args[@"on"] boolValue]];
        reply[@"ok"] = @(ok);
        if (!ok) reply[@"error"] = @"无法写入登录项";
        data = [AFBridge appSettings];
    } else if ([cmd isEqualToString:@"setStartMinimized"]) {
        [AFBridge setStartMinimized:[args[@"on"] boolValue]];
        data = [AFBridge appSettings];
    } else if ([cmd isEqualToString:@"setMenuTempSource"]) {
        [AFBridge setMenuTempSource:[args[@"source"] integerValue]];
        data = [AFBridge appSettings];
        if (_onMenuTempSourceChanged) _onMenuTempSourceChanged();
    } else if ([cmd isEqualToString:@"setBoost"]) {
        /* 手动档位与曲线引擎互斥：点固定档位即视为人工接管风扇。
           不先停引擎的话，timer 下一拍会按温度曲线覆盖刚下发的 boost，
           表现就是「点了多级调节没反应、关掉曲线才生效」。 */
        BOOL wasRunning = _engine.running;
        if (wasRunning) {
            [_engine stop];
            if (_onEngineChanged) _onEngineChanged(NO);
        }
        int index = [args[@"index"] intValue];
        int pct = [args[@"pct"] intValue];
        pct = MAX(0, MIN(100, pct));
        BOOL ok = index < 0 ? [_engine setFixedBoost:pct] : [_engine setFanBoost:index percent:pct];
        reply[@"ok"] = @(ok);
        if (!ok) reply[@"error"] = _engine.lastError ?: @"下发失败";
        data = @{@"curveStopped": @(wasRunning)};
    } else if ([cmd isEqualToString:@"setPower"]) {
        BOOL ok = [_engine setPowerRaw:[args[@"raw"] intValue]];
        reply[@"ok"] = @(ok);
        if (!ok) reply[@"error"] = _engine.lastError ?: @"设置模式失败";
    } else if ([cmd isEqualToString:@"setGMode"]) {
        BOOL ok = [_engine setGMode:[args[@"on"] intValue]];
        reply[@"ok"] = @(ok);
        if (!ok) reply[@"error"] = _engine.lastError ?: @"G 模式设置失败";
    } else if ([cmd isEqualToString:@"raw"]) {
        int r = [_engine rawCom:[args[@"com"] intValue] sub:[args[@"sub"] intValue]
                             a1:[args[@"a1"] intValue] a2:[args[@"a2"] intValue]];
        data = @{@"value": @(r), @"hex": [NSString stringWithFormat:@"0x%02X", (unsigned)r & 0xff]};
    } else if ([cmd isEqualToString:@"engine"]) {
        NSString *op = args[@"op"] ?: @"";
        if ([op isEqualToString:@"start"]) {
            BOOL ok = [_engine start];
            reply[@"ok"] = @(ok);
            if (!ok) reply[@"error"] = _engine.lastError ?: @"启动失败";
        } else if ([op isEqualToString:@"stop"]) {
            [_engine stop];
        } else if ([op isEqualToString:@"apply"]) {
            /* 「立即应用」= 让当前曲线真正生效并持续运行，而不只是补发一拍。
               引擎未运行时先 start（start 内部已 tick 一次），再强制刷新一次。 */
            if (!_engine.running) {
                BOOL ok = [_engine start];
                reply[@"ok"] = @(ok);
                if (!ok) reply[@"error"] = _engine.lastError ?: @"启动失败";
            }
            if (_engine.running) [_engine applyOnce];
        }
        if (_onEngineChanged) _onEngineChanged(_engine.running);
    } else if ([cmd isEqualToString:@"setEngineParams"]) {
        if (args[@"interval"]) _engine.interval = [args[@"interval"] doubleValue];
        if (args[@"maxStep"])  _engine.maxStep  = [args[@"maxStep"] doubleValue];
    } else if ([cmd isEqualToString:@"setActivePreset"]) {
        AFPreset *p = [AFPreset fromDict:args[@"preset"]];
        if (p) {
            _engine.activePreset = p;
            NSUInteger idx = [self.presets indexOfObjectPassingTest:^BOOL(AFPreset *x, NSUInteger i, BOOL *s) {
                return [x.name isEqualToString:p.name];
            }];
            if (idx != NSNotFound) self.presets[idx] = p;
        }
    } else if ([cmd isEqualToString:@"savePreset"]) {
        AFPreset *p = [AFPreset fromDict:args[@"preset"]];
        if (p) {
            NSUInteger idx = [self.presets indexOfObjectPassingTest:^BOOL(AFPreset *x, NSUInteger i, BOOL *s) {
                return [x.name isEqualToString:p.name];
            }];
            if (idx != NSNotFound) self.presets[idx] = p; else [self.presets addObject:p];
            _activeName = p.name;
            _engine.activePreset = p;
            [self persistPresets];
            data = [p toDict];
        }
    } else if ([cmd isEqualToString:@"deletePreset"]) {
        NSString *name = args[@"name"];
        NSUInteger idx = [self.presets indexOfObjectPassingTest:^BOOL(AFPreset *x, NSUInteger i, BOOL *s) {
            return [x.name isEqualToString:name];
        }];
        if (idx != NSNotFound) [self.presets removeObjectAtIndex:idx];
        if (self.presets.count == 0) self.presets = [[self builtinPresets] mutableCopy];
        if (![self presetNamed:_activeName]) {
            _activeName = ((AFPreset *)self.presets[0]).name;
            _engine.activePreset = [self presetNamed:_activeName];
        }
        [self persistPresets];
    } else if ([cmd isEqualToString:@"activatePreset"]) {
        AFPreset *p = [self presetNamed:args[@"name"]];
        if (!p) {
            reply[@"ok"] = @NO;
            reply[@"error"] = @"未找到该预设";
        } else {
            [self activatePresetNamed:args[@"name"]];
            /* 让「载入后立即生效」名副其实：曲线预设自动接管引擎，
               固定 boost 预设直接下发。原来只在引擎已运行时 applyOnce，
               停用状态下载入预设只换了名字、风扇毫无变化。 */
            if (p.useCurve) {
                if (!_engine.running) {
                    BOOL ok = [_engine start];
                    reply[@"ok"] = @(ok);
                    if (!ok) reply[@"error"] = _engine.lastError ?: @"启动失败";
                } else {
                    [_engine applyOnce];
                }
            } else {
                BOOL ok = [_engine setFixedBoost:p.fixedBoost];
                reply[@"ok"] = @(ok);
                if (!ok) reply[@"error"] = _engine.lastError ?: @"下发失败";
                if (_engine.running) [_engine stop];
            }
            if (_onEngineChanged) _onEngineChanged(_engine.running);
        }
    } else if ([cmd isEqualToString:@"resetPresets"]) {
        [self resetToBuiltins];
    } else if ([cmd isEqualToString:@"restoreBios"]) {
        [_engine stop];
        [_engine setFixedBoost:0];
        if (_onEngineChanged) _onEngineChanged(NO);
    } else if ([cmd isEqualToString:@"revealConfig"]) {
        NSString *dir = [_engine configDir];
        [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:dir]];
    }

    if (data) reply[@"data"] = data;
    [self sendToJS:@"__afReply" args:@[ident, reply]];
}

#pragma mark - 状态

- (NSDictionary *)stateSnapshot
{
    AFInfo inf = _engine.info;
    AFFan fans[AF_MAX_FANS] = {0};
    int temps[AF_MAX_SENSORS] = {0};
    int powers[AF_MAX_POWERS] = {0};
    int nf = [_engine readFans:fans max:AF_MAX_FANS];
    int nt = [_engine readTemps:temps max:AF_MAX_SENSORS];
    int np = [_engine readPowers:powers max:AF_MAX_POWERS];

    NSMutableArray *fanArr = [NSMutableArray array];
    for (int i = 0; i < nf; i++)
        [fanArr addObject:@{@"index": @(i), @"rpm": @(fans[i].rpm),
                            @"maxRpm": @(fans[i].maxRpm), @"boost": @(fans[i].boost),
                            @"percent": @(fans[i].percent)}];

    NSMutableArray *tempArr = [NSMutableArray array];
    for (int i = 0; i < nt; i++) [tempArr addObject:@(temps[i])];

    NSMutableArray *powerArr = [NSMutableArray array];
    for (int i = 0; i < np; i++) [powerArr addObject:@(powers[i])];

    NSMutableArray *presetArr = [NSMutableArray array];
    for (AFPreset *p in self.presets) [presetArr addObject:[p toDict]];

    return @{
        @"connected": @(_engine.connected),
        @"running": @(_engine.running),
        @"info": @{@"fans": @(inf.fans), @"sensors": @(inf.sensors),
                   @"powers": @(inf.powers), @"hasGmode": @(inf.hasGmode)},
        @"fans": fanArr,
        @"temps": tempArr,
        @"powers": powerArr,
        @"currentPower": @([_engine currentPowerRaw]),
        @"currentGMode": @([_engine currentGMode]),
        @"presets": presetArr,
        @"activeName": self.activeName ?: @"",
        @"configDir": [_engine configDir],
        @"interval": @(_engine.interval),
        @"maxStep": @(_engine.maxStep),
        @"language": [AFBridge currentLanguage],
        @"appVersion": [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleShortVersionString"] ?: @"?",
        @"requiredDriverVersion": AFRequiredDriverVersion,
        @"settings": [AFBridge appSettings],
        @"error": _engine.lastError ?: @""
    };
}

- (void)pushTelemetry
{
    if (!self.webView) return;
    NSDictionary *snap = [self stateSnapshot];
    /* 单对象参数：JS 侧 __afPush(state) 期望字典本身，而非包一层数组 */
    NSString *script = [NSString stringWithFormat:@"if(typeof __afPush==='function')__afPush(%@);",
                        [self jsonLiteral:snap]];
    [_webView evaluateJavaScript:script completionHandler:^(id result, NSError *error) {
        if (error) NSLog(@"[AFBridge] __afPush 失败: %@", error.localizedDescription);
    }];
}

- (void)pushStatus:(NSString *)text warn:(BOOL)warn
{
    [self sendToJS:@"__afStatus" args:@[text ?: @"", @(warn)]];
}

#pragma mark - JS 调用工具

- (void)sendToJS:(NSString *)fn args:(NSArray *)args
{
    if (!self.webView || !fn) return;
    /* 逐个参数序列化，保持 JS 侧函数签名（如 __afReply(id, reply)） */
    NSMutableArray *lits = [NSMutableArray array];
    for (id arg in args) [lits addObject:[self jsonLiteral:arg]];
    NSString *argList = [lits componentsJoinedByString:@","];
    NSString *script = [NSString stringWithFormat:
                        @"if(typeof %@==='function')%@(%@);", fn, fn, argList];
    void (^run)(void) = ^{
        [self.webView evaluateJavaScript:script completionHandler:^(id result, NSError *error) {
            if (error) NSLog(@"[AFBridge] JS 调用失败 %@: %@", fn, error.localizedDescription);
        }];
    };
    if ([NSThread isMainThread]) run();
    else dispatch_async(dispatch_get_main_queue(), run);
}

- (NSString *)jsonLiteral:(id)obj
{
    /* 允许顶层片段（裸数字/字符串），否则 NSNumber 会抛异常 */
    NSData *data = [NSJSONSerialization dataWithJSONObject:obj
                                                   options:NSJSONWritingFragmentsAllowed
                                                     error:nil];
    if (!data) return @"null";
    NSString *s = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    s = [s stringByReplacingOccurrencesOfString:@"\\" withString:@"\\\\"];
    s = [s stringByReplacingOccurrencesOfString:@"'" withString:@"\\'"];
    s = [s stringByReplacingOccurrencesOfString:@"\n" withString:@"\\n"];
    s = [s stringByReplacingOccurrencesOfString:@"\u2028" withString:@"\\u2028"];
    s = [s stringByReplacingOccurrencesOfString:@"\u2029" withString:@"\\u2029"];
    return [NSString stringWithFormat:@"JSON.parse('%@')", s];
}

@end
