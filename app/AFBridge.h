/*
 * AFBridge — Web UI 与 AFCurveEngine 之间的桥接层
 *
 * JS 侧调用： window.webkit.messageHandlers.af.postMessage({id, cmd, args})
 * 原生回复： window.__afReply(id, {ok, data|error})
 * 原生推送： window.__afPush(telemetry) / window.__afStatus(text, warn)
 */
#import <Foundation/Foundation.h>
#import <WebKit/WebKit.h>

@class AFCurveEngine, AFPreset;

@interface AFBridge : NSObject <WKScriptMessageHandler>

@property (nonatomic, weak)   WKWebView *webView;
@property (nonatomic, strong) AFCurveEngine *engine;
@property (nonatomic, strong) NSMutableArray<AFPreset *> *presets;
@property (nonatomic, copy)   NSString *activeName;

/* 预设或状态变化时通知原生侧（刷新菜单栏） */
@property (nonatomic, copy) void (^onPresetsChanged)(void);
@property (nonatomic, copy) void (^onEngineChanged)(BOOL running);
/* 界面语言切换后通知原生侧（刷新菜单栏、窗口标题） */
@property (nonatomic, copy) void (^onLanguageChanged)(NSString *language);
/* 状态栏温度来源变化后通知原生侧（立即刷新菜单栏标题与勾选） */
@property (nonatomic, copy) void (^onMenuTempSourceChanged)(void);

/* 当前界面语言：zh / en。未设置时跟随系统首选语言 */
+ (NSString *)currentLanguage;

#pragma mark - 启动与状态栏设置
/* 全部启动相关设置，供 Web UI 初始化与回读 */
+ (NSDictionary *)appSettings;
/* 开机自启：通过 ~/Library/LaunchAgents 的用户级 plist 实现，无需管理员权限 */
+ (BOOL)isLaunchAtLogin;
+ (BOOL)setLaunchAtLogin:(BOOL)on;
/* 启动时最小化到状态栏 */
+ (BOOL)startMinimized;
+ (void)setStartMinimized:(BOOL)on;
/* 状态栏温度来源：0 = 最高温, 1 = CPU, 2 = 显卡 */
+ (NSInteger)menuTempSource;
+ (void)setMenuTempSource:(NSInteger)source;

- (instancetype)initWithEngine:(AFCurveEngine *)engine;

/* 每秒推送实时读数 */
- (void)pushTelemetry;
- (void)pushStatus:(NSString *)text warn:(BOOL)warn;

/* 供菜单栏调用 */
- (void)activatePresetNamed:(NSString *)name;
- (NSArray<AFPreset *> *)builtinPresets;
- (void)resetToBuiltins;
@end
