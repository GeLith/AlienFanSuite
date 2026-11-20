/*
 * afclient - AlienFan.kext 用户态通信库
 *
 * 不依赖 root，直接通过 IOUserClient 读写。
 * 选择器必须与 AlienFan.hpp 的 kAlienFan* 枚举一致。
 */
#ifndef AFCLIENT_H
#define AFCLIENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    kAFProbe = 0,
    kAFInfo,
    kAFGetFan,
    kAFFanCount,
    kAFFanId,
    kAFSetBoost,
    kAFGetPower,
    kAFSetPower,
    kAFGetGMode,
    kAFSetGMode,
    kAFRaw,
    kAFSensorCount,
    kAFSensor,
    kAFPowerList
};

#define AF_MAX_FANS    4
#define AF_MAX_SENSORS 16
#define AF_MAX_POWERS  8

typedef struct {
    int ready;
    int device;       /* -2 = provider 直连 */
    int systemId;
    int fans;
    int sensors;
    int powers;
    int hasGmode;
    int gmode;
} AFInfo;

typedef struct {
    int rpm;
    int percent;      /* 本机不支持，恒为 -2 */
    int boost;
    int maxRpm;
} AFFan;

/* 驱动安装/加载状态：不需要打开 UserClient 就能采集，
   供设置面板在驱动未连接时也能给出诊断信息 */
typedef struct {
    int  instances;       /* IORegistry 中 AlienFan 服务实例数 */
    int  readyInstances;  /* Ready=1 的实例数 */
    int  fanCount;        /* kext 探测到的风扇数 */
    int  sensorCount;
    int  powerCount;
    int  systemId;
    char role[32];        /* provider / scan / none */
    char diag[512];       /* kext 写入的探测轨迹 */
    char device[64];      /* ACPI 设备路径 */
    char bundleVersion[32]; /* IORegistry 里的 CFBundleVersion（可能为空） */
} AFDriverScan;

typedef struct AFClient AFClient;

/* 扫描驱动实例（不打开 UserClient）。返回实例数，出错返回 -1 */
int af_scan(AFDriverScan *out);

/* 打开驱动。失败返回 NULL，可用 af_last_error() 取描述 */
AFClient *af_open(void);
void      af_close(AFClient *c);
const char *af_last_error(void);

int af_info(AFClient *c, AFInfo *out);
int af_fans(AFClient *c, AFFan *out, int max);          /* 返回填充数 */
int af_fan_ids(AFClient *c, int *out, int max);
int af_temps(AFClient *c, int *out, int max);           /* 返回填充数 */
int af_powers(AFClient *c, int *out, int max);          /* 返回填充数（原始码） */

int af_get_power(AFClient *c, int *raw, int *index);
int af_set_power_raw(AFClient *c, int raw);             /* 传原始码，不传下标 */

int af_get_gmode(AFClient *c, int *on);
int af_set_gmode(AFClient *c, int on);

int af_set_boost(AFClient *c, int fanIndex, int percent);
int af_set_all_boost(AFClient *c, int percent, int fanCount);

/* 原始 WMAX，给高级面板用 */
int af_raw(AFClient *c, int com, int sub, int a1, int a2);

#ifdef __cplusplus
}
#endif
#endif
