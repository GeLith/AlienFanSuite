/*
 * 风扇曲线引擎 + 预设存储
 */
#import <Foundation/Foundation.h>
#import "afclient.h"

extern NSString * const AFCurveDidTickNotification;

@interface AFCurvePoint : NSObject <NSCopying>
@property (nonatomic) CGFloat temp;   /* °C */
@property (nonatomic) CGFloat boost;  /* 0-100 % */
- (instancetype)initWithTemp:(CGFloat)t boost:(CGFloat)b;
@end

@interface AFPreset : NSObject <NSCopying>
@property (nonatomic, copy)   NSString *name;
@property (nonatomic, strong) NSMutableArray<AFCurvePoint *> *points;
@property (nonatomic)         NSInteger powerRaw;   /* -1 = 不改 */
@property (nonatomic)         NSInteger gmode;      /* -1 = 不改, 0/1 */
@property (nonatomic)         BOOL       useCurve;
@property (nonatomic)         NSInteger fixedBoost; /* useCurve=NO 时用 */

+ (AFPreset *)presetWithName:(NSString *)name;
+ (AFPreset *)defaultBalanced;
+ (AFPreset *)defaultQuiet;
+ (AFPreset *)defaultPerformance;
- (CGFloat)boostForTemp:(CGFloat)temp;
- (NSDictionary *)toDict;
+ (AFPreset *)fromDict:(NSDictionary *)d;
@end

@interface AFCurveEngine : NSObject
@property (nonatomic, readonly) BOOL connected;
@property (nonatomic, readonly) BOOL running;
@property (nonatomic, readonly) AFInfo info;
@property (nonatomic, strong)   AFPreset *activePreset;
@property (nonatomic)           CGFloat maxStep;     /* 每次最多变化多少 %，默认 12 */
@property (nonatomic)           NSTimeInterval interval; /* 默认 2.0s */
@property (nonatomic, copy)     NSString *lastError;

+ (instancetype)shared;

/* 曲线引擎启停状态的记忆（跨重启恢复，保证按钮显示与实际运行一致） */
+ (BOOL)curveEnabledRemembered;
+ (void)setCurveEnabledRemembered:(BOOL)on;

- (BOOL)connect;
- (void)disconnect;

/* 刷新实时读数 */
- (int)readFans:(AFFan *)out max:(int)max;
- (int)readTemps:(int *)out max:(int)max;
- (int)readPowers:(int *)out max:(int)max;
- (int)currentPowerRaw;
- (int)currentGMode;

/* 曲线控制 */
- (BOOL)start;
- (void)stop;
- (void)tick;                     /* 手动触发一次采样+下发 */
- (void)applyOnce;                /* 立即应用当前预设（不做平滑） */

/* 直接操作（脱离曲线） */
- (BOOL)setFixedBoost:(int)pct;
- (BOOL)setFanBoost:(int)index percent:(int)pct;   /* 单风扇调节 */
- (BOOL)setPowerRaw:(int)raw;
- (BOOL)setGMode:(int)on;

/* 原始 WMAX（高级面板） */
- (int)rawCom:(int)com sub:(int)sub a1:(int)a1 a2:(int)a2;

/* 预设持久化 */
- (NSArray<AFPreset *> *)loadPresets;
- (NSString *)loadActiveName;
- (void)savePresets:(NSArray<AFPreset *> *)list activeName:(NSString *)name;
- (NSString *)configDir;
@end
