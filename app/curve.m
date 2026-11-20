#import "curve.h"
#include <math.h>
#include <stdlib.h>

NSString * const AFCurveDidTickNotification = @"AFCurveDidTick";

/* 曲线启停状态记忆：写入 NSUserDefaults，重启后据此恢复引擎，避免
   “按钮显示启用曲线，实际风扇却是别的策略在驱动”的状态漂移。 */
static NSString * const AFCurveEnabledKey = @"AlienFanCurveEnabled";

/* ---------------- 曲线点 ---------------- */

@implementation AFCurvePoint
- (instancetype)initWithTemp:(CGFloat)t boost:(CGFloat)b
{
    self = [super init];
    if (self) { _temp = t; _boost = b; }
    return self;
}
- (id)copyWithZone:(NSZone *)zone
{
    return [[AFCurvePoint alloc] initWithTemp:_temp boost:_boost];
}
@end

/* ---------------- 预设 ---------------- */

@implementation AFPreset

+ (AFPreset *)presetWithName:(NSString *)name
{
    AFPreset *p = [AFPreset new];
    p.name = name;
    p.powerRaw = -1;
    p.gmode = -1;
    p.useCurve = YES;
    p.fixedBoost = 60;
    return p;
}

+ (AFPreset *)defaultQuiet
{
    /* 12 点，30–100°C 密集采样：优先安静，高温才逐步拉升 */
    AFPreset *p = [AFPreset presetWithName:@"静音"];
    p.points = [@[
        [[AFCurvePoint alloc] initWithTemp:30 boost:5],
        [[AFCurvePoint alloc] initWithTemp:37 boost:8],
        [[AFCurvePoint alloc] initWithTemp:44 boost:12],
        [[AFCurvePoint alloc] initWithTemp:51 boost:18],
        [[AFCurvePoint alloc] initWithTemp:58 boost:25],
        [[AFCurvePoint alloc] initWithTemp:64 boost:34],
        [[AFCurvePoint alloc] initWithTemp:70 boost:45],
        [[AFCurvePoint alloc] initWithTemp:76 boost:58],
        [[AFCurvePoint alloc] initWithTemp:82 boost:70],
        [[AFCurvePoint alloc] initWithTemp:88 boost:82],
        [[AFCurvePoint alloc] initWithTemp:94 boost:92],
        [[AFCurvePoint alloc] initWithTemp:100 boost:100]
    ] mutableCopy];
    return p;
}

+ (AFPreset *)defaultBalanced
{
    /* 12 点：噪音与散热折中，中温段平稳爬升 */
    AFPreset *p = [AFPreset presetWithName:@"均衡"];
    p.points = [@[
        [[AFCurvePoint alloc] initWithTemp:30 boost:15],
        [[AFCurvePoint alloc] initWithTemp:37 boost:20],
        [[AFCurvePoint alloc] initWithTemp:44 boost:28],
        [[AFCurvePoint alloc] initWithTemp:51 boost:37],
        [[AFCurvePoint alloc] initWithTemp:58 boost:48],
        [[AFCurvePoint alloc] initWithTemp:64 boost:60],
        [[AFCurvePoint alloc] initWithTemp:70 boost:72],
        [[AFCurvePoint alloc] initWithTemp:76 boost:82],
        [[AFCurvePoint alloc] initWithTemp:82 boost:90],
        [[AFCurvePoint alloc] initWithTemp:88 boost:96],
        [[AFCurvePoint alloc] initWithTemp:94 boost:100],
        [[AFCurvePoint alloc] initWithTemp:100 boost:100]
    ] mutableCopy];
    return p;
}

+ (AFPreset *)defaultPerformance
{
    /* 12 点：低温即高转速，76°C 后拉满 */
    AFPreset *p = [AFPreset presetWithName:@"性能"];
    p.points = [@[
        [[AFCurvePoint alloc] initWithTemp:30 boost:55],
        [[AFCurvePoint alloc] initWithTemp:37 boost:62],
        [[AFCurvePoint alloc] initWithTemp:44 boost:70],
        [[AFCurvePoint alloc] initWithTemp:51 boost:78],
        [[AFCurvePoint alloc] initWithTemp:58 boost:85],
        [[AFCurvePoint alloc] initWithTemp:64 boost:91],
        [[AFCurvePoint alloc] initWithTemp:70 boost:96],
        [[AFCurvePoint alloc] initWithTemp:76 boost:100],
        [[AFCurvePoint alloc] initWithTemp:82 boost:100],
        [[AFCurvePoint alloc] initWithTemp:88 boost:100],
        [[AFCurvePoint alloc] initWithTemp:94 boost:100],
        [[AFCurvePoint alloc] initWithTemp:100 boost:100]
    ] mutableCopy];
    return p;
}

- (CGFloat)boostForTemp:(CGFloat)temp
{
    if (_points.count == 0) return 0;
    NSArray *sorted = [_points sortedArrayUsingComparator:^NSComparisonResult(AFCurvePoint *a, AFCurvePoint *b) {
        return a.temp < b.temp ? NSOrderedAscending : (a.temp > b.temp ? NSOrderedDescending : NSOrderedSame);
    }];

    AFCurvePoint *first = sorted[0];
    if (temp <= first.temp) return first.boost;

    AFCurvePoint *last = sorted.lastObject;
    if (temp >= last.temp) return last.boost;

    for (NSInteger i = 1; i < (NSInteger)sorted.count; i++) {
        AFCurvePoint *a = sorted[i-1], *b = sorted[i];
        if (temp >= a.temp && temp <= b.temp) {
            CGFloat span = b.temp - a.temp;
            if (span <= 0) return b.boost;
            CGFloat t = (temp - a.temp) / span;
            return a.boost + (b.boost - a.boost) * t;
        }
    }
    return last.boost;
}

- (NSDictionary *)toDict
{
    NSMutableArray *pts = [NSMutableArray array];
    for (AFCurvePoint *p in _points)
        [pts addObject:@[@(p.temp), @(p.boost)]];
    return @{
        @"name": _name ?: @"未命名",
        @"useCurve": @(_useCurve),
        @"fixedBoost": @(_fixedBoost),
        @"powerRaw": @(_powerRaw),
        @"gmode": @(_gmode),
        @"points": pts
    };
}

+ (AFPreset *)fromDict:(NSDictionary *)d
{
    if (![d isKindOfClass:[NSDictionary class]]) return nil;
    AFPreset *p = [AFPreset presetWithName:d[@"name"] ?: @"未命名"];
    p.useCurve = [d[@"useCurve"] boolValue];
    p.fixedBoost = [d[@"fixedBoost"] integerValue];
    p.powerRaw = d[@"powerRaw"] ? [d[@"powerRaw"] integerValue] : -1;
    p.gmode = d[@"gmode"] ? [d[@"gmode"] integerValue] : -1;

    NSMutableArray *pts = [NSMutableArray array];
    for (NSArray *pair in (NSArray *)d[@"points"] ?: @[]) {
        if (![pair isKindOfClass:[NSArray class]] || pair.count < 2) continue;
        [pts addObject:[[AFCurvePoint alloc] initWithTemp:[pair[0] doubleValue]
                                                    boost:[pair[1] doubleValue]]];
    }
    p.points = pts;
    return p;
}

- (id)copyWithZone:(NSZone *)zone
{
    AFPreset *p = [AFPreset presetWithName:_name];
    p.points = [[NSMutableArray alloc] initWithArray:_points copyItems:YES];
    p.powerRaw = _powerRaw;
    p.gmode = _gmode;
    p.useCurve = _useCurve;
    p.fixedBoost = _fixedBoost;
    return p;
}
@end

/* ---------------- 引擎 ---------------- */

@interface AFCurveEngine ()
@property (nonatomic) AFClient *client;
@property (nonatomic) NSTimer *timer;
@property (nonatomic) NSInteger appliedBoost;
@property (nonatomic) NSInteger lastPowerRaw;
@property (nonatomic) NSInteger lastGMode;
@end

@implementation AFCurveEngine

+ (BOOL)curveEnabledRemembered
{
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    /* 未写过时默认启用：符合“开机希望风扇转快些”的产品意图 */
    if ([ud objectForKey:AFCurveEnabledKey] == nil) return YES;
    return [ud boolForKey:AFCurveEnabledKey];
}

+ (void)setCurveEnabledRemembered:(BOOL)on
{
    [[NSUserDefaults standardUserDefaults] setBool:on forKey:AFCurveEnabledKey];
}

+ (instancetype)shared
{
    static AFCurveEngine *s = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        s = [AFCurveEngine new];
        s->_interval = 2.0;
        s->_maxStep = 12;
        s->_appliedBoost = -1;
        s->_lastPowerRaw = -999;
        s->_lastGMode = -999;
    });
    return s;
}

- (NSString *)configDir
{
    NSString *d = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/AlienFan"];
    NSFileManager *fm = [NSFileManager defaultManager];
    if (![fm fileExistsAtPath:d])
        [fm createDirectoryAtPath:d withIntermediateDirectories:YES attributes:nil error:nil];
    return d;
}

- (NSString *)configPath
{
    return [[self configDir] stringByAppendingPathComponent:@"presets.json"];
}

- (BOOL)connect
{
    if (_client) return YES;
    _client = af_open();
    if (!_client) {
        self.lastError = [NSString stringWithUTF8String:af_last_error()];
        return NO;
    }
    return YES;
}

- (BOOL)connected { return _client != NULL; }

- (void)disconnect
{
    [self stop];
    if (_client) { af_close(_client); _client = NULL; }
}

- (AFInfo)info
{
    AFInfo i = {0};
    if (_client) af_info(_client, &i);
    return i;
}

- (int)readFans:(AFFan *)out max:(int)max
{
    return _client ? af_fans(_client, out, max) : 0;
}

- (int)readTemps:(int *)out max:(int)max
{
    return _client ? af_temps(_client, out, max) : 0;
}

- (int)readPowers:(int *)out max:(int)max
{
    return _client ? af_powers(_client, out, max) : 0;
}

- (int)currentPowerRaw
{
    int raw = -1, idx = -1;
    if (_client) af_get_power(_client, &raw, &idx);
    return raw;
}

- (int)currentGMode
{
    int on = 0;
    if (_client && af_get_gmode(_client, &on) == 0) return on;
    return 0;
}

- (BOOL)start
{
    if (_running) return YES;
    if (![self connect]) return NO;
    _running = YES;
    [AFCurveEngine setCurveEnabledRemembered:YES];
    [self tick];
    _timer = [NSTimer scheduledTimerWithTimeInterval:_interval
                                              target:self
                                            selector:@selector(tick)
                                            userInfo:nil
                                             repeats:YES];
    return YES;
}

- (void)stop
{
    [_timer invalidate];
    _timer = nil;
    _running = NO;
    [AFCurveEngine setCurveEnabledRemembered:NO];
}

/* 运行中调整采样间隔必须立即重建 timer，否则界面显示改了、实际仍按旧频率采样 */
- (void)setInterval:(NSTimeInterval)interval
{
    if (interval <= 0) interval = 0.5;
    if (_interval == interval) return;
    BOOL wasRunning = _running;
    if (wasRunning) {
        [_timer invalidate];
        _timer = nil;
    }
    _interval = interval;
    if (wasRunning) {
        _timer = [NSTimer scheduledTimerWithTimeInterval:_interval
                                                  target:self
                                                selector:@selector(tick)
                                                userInfo:nil
                                                 repeats:YES];
    }
}

- (void)tick
{
    if (!_client || !_activePreset) return;

    AFInfo inf;
    af_info(_client, &inf);
    if (inf.fans <= 0) return;

    /* 一次模式设置即可，避免每 2 秒重复下发 */
    if (_activePreset.powerRaw >= 0 && _activePreset.powerRaw != _lastPowerRaw) {
        af_set_power_raw(_client, (int)_activePreset.powerRaw);
        _lastPowerRaw = _activePreset.powerRaw;
    }
    if (_activePreset.gmode >= 0 && _activePreset.gmode != _lastGMode) {
        af_set_gmode(_client, (int)_activePreset.gmode);
        _lastGMode = _activePreset.gmode;
    }

    /* 只下发一次模式后，boost 必须重新下发（切模式会清 boost） */
    CGFloat target = 0;
    if (_activePreset.useCurve) {
        int temps[AF_MAX_SENSORS] = {0};
        int n = af_temps(_client, temps, AF_MAX_SENSORS);
        CGFloat hottest = -1000;
        for (int i = 0; i < n; i++)
            if (temps[i] > hottest) hottest = temps[i];
        if (hottest < -500) return;
        target = [_activePreset boostForTemp:hottest];
    } else {
        target = _activePreset.fixedBoost;
    }

    /* 平滑：避免温度抖动导致风扇忽快忽慢 */
    NSInteger t = (NSInteger)lround(target);
    if (_appliedBoost >= 0 && labs(t - _appliedBoost) > _maxStep) {
        t = _appliedBoost + (t > _appliedBoost ? _maxStep : -_maxStep);
    }
    t = MAX(0, MIN(100, t));

    af_set_all_boost(_client, (int)t, inf.fans);
    _appliedBoost = t;

    [[NSNotificationCenter defaultCenter] postNotificationName:AFCurveDidTickNotification object:self];
}

- (void)applyOnce
{
    _appliedBoost = -1;
    _lastPowerRaw = -999;
    _lastGMode = -999;
    [self tick];
}

- (BOOL)setFixedBoost:(int)pct
{
    if (![self connect]) return NO;
    AFInfo inf; af_info(_client, &inf);
    int rc = af_set_all_boost(_client, pct, inf.fans);
    self.lastError = rc == 0 ? nil : @"下发 boost 失败";
    return rc == 0;
}

- (BOOL)setFanBoost:(int)index percent:(int)pct
{
    if (![self connect]) return NO;
    int rc = af_set_boost(_client, index, pct);
    self.lastError = rc == 0 ? nil : @"单风扇下发失败";
    return rc == 0;
}

- (BOOL)setPowerRaw:(int)raw
{
    if (![self connect]) return NO;
    int rc = af_set_power_raw(_client, raw);
    self.lastError = rc == 0 ? nil : @"设置模式失败（检查原始码是否合法）";
    return rc == 0;
}

- (BOOL)setGMode:(int)on
{
    if (![self connect]) return NO;
    int rc = af_set_gmode(_client, on);
    if (rc != 0) {
        /* 兜底：某些 BIOS 只能靠设 0xAB 进 G 模式 */
        rc = af_set_power_raw(_client, 0xAB);
    }
    self.lastError = rc == 0 ? nil : @"G 模式设置失败";
    return rc == 0;
}

- (int)rawCom:(int)com sub:(int)sub a1:(int)a1 a2:(int)a2
{
    if (![self connect]) return -1;
    return af_raw(_client, com, sub, a1, a2);
}

#pragma mark - 预设持久化

- (NSArray<AFPreset *> *)loadPresets
{
    NSData *data = [NSData dataWithContentsOfFile:[self configPath]];
    if (!data) {
        return @[[AFPreset defaultQuiet], [AFPreset defaultBalanced], [AFPreset defaultPerformance]];
    }
    NSDictionary *root = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
    if (![root isKindOfClass:[NSDictionary class]]) return @[];

    NSMutableArray *list = [NSMutableArray array];
    for (NSDictionary *d in root[@"presets"] ?: @[]) {
        AFPreset *p = [AFPreset fromDict:d];
        if (p) [list addObject:p];
    }
    return list;
}

- (NSString *)loadActiveName
{
    NSData *data = [NSData dataWithContentsOfFile:[self configPath]];
    if (!data) return @"性能";
    NSDictionary *root = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
    return root[@"active"] ?: @"性能";
}

- (void)savePresets:(NSArray<AFPreset *> *)list activeName:(NSString *)name
{
    NSMutableArray *arr = [NSMutableArray array];
    for (AFPreset *p in list) [arr addObject:[p toDict]];
    NSDictionary *root = @{ @"presets": arr, @"active": name ?: @"" };
    NSData *data = [NSJSONSerialization dataWithJSONObject:root options:NSJSONWritingPrettyPrinted error:nil];
    [data writeToFile:[self configPath] atomically:YES];
}

@end
