/*
 * AlienFan.hpp - Alienware / Dell 风扇控制 kext（ACPI 通道）
 *
 * 本文件是 alien-tools (T-Troll) v1 SDK 的 ACPI 分支在 macOS 上的移植。
 * 协议来源：alienfan-SDK/alienfan-SDK.cpp + alienfan-controls.h
 *
 * 调用形状（与 Windows 端 EvalAcpiMethod 完全一致）：
 *      WMAX(int 0, int com, buffer[4]{ sub, arg1, arg2, 0 })
 * 返回值：ACPI Integer（Argument[0].Argument）
 *
 * 与 Windows 端的差异只在“怎么进 ACPI”：那边是自带内核驱动 + IOCTL，
 * 这边走 IOACPIPlatformDevice::evaluateObject。
 */

#ifndef _ALIENFAN_HPP
#define _ALIENFAN_HPP

#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>

#define ALIENFAN_MAX_FANS     4
#define ALIENFAN_MAX_SENSORS  16
#define ALIENFAN_MAX_POWERS   8
#define ALIENFAN_DEVICE_COUNT 5
#define ALIENFAN_SCAN_LIMIT   64

/* 一条 ACPI 子命令：主命令号 + 子命令号 */
struct AFCommand {
    UInt8 com;
    UInt8 sub;
};

/* 设备表条目（对应 devs[]） */
struct AFDevice {
    const char *name;     /* ACPI 平面里的节点名，如 AMWW */
    const char *path;     /* 原始完整路径，仅用于日志 */
    const char *gpuPath;
    SInt16      delta;    /* com 偏移：AMW1/AMW3/AMWW 为 0，Aurora/Area51 为 4 */
};

/* 控制表下标（对应 dev_controls 的 14 个字段） */
enum {
    kCtlGetPowerID = 0,
    kCtlGetFanRPM,
    kCtlGetFanPercent,
    kCtlGetFanBoost,
    kCtlSetFanBoost,
    kCtlGetTemp,
    kCtlGetPower,
    kCtlSetPower,
    kCtlSetGPUPower,
    kCtlGetGMode,
    kCtlSetGMode,
    kCtlGetSystemID,
    kCtlGetFanType,
    kCtlGetMaxRPM,
    kCtlCount
};

/* 用户态选择子，与 alienfanctl 保持同步 */
enum {
    kAlienFanProbe = 0,
    kAlienFanInfo,
    kAlienFanGetFan,
    kAlienFanGetFanCount,
    kAlienFanGetFanId,
    kAlienFanSetBoost,
    kAlienFanGetPower,
    kAlienFanSetPower,
    kAlienFanGetGMode,
    kAlienFanSetGMode,
    kAlienFanRaw,
    kAlienFanGetSensorCount,
    kAlienFanGetSensor,
    kAlienFanGetPowerList,
    kAlienFanMethodCount
};

class AlienFan : public IOService
{
    OSDeclareDefaultStructors(AlienFan)
    friend class AlienFanUserClient;

public:
    virtual bool     init(OSDictionary *dict) APPLE_KEXT_OVERRIDE;
    virtual bool     start(IOService *provider) APPLE_KEXT_OVERRIDE;
    virtual void     stop(IOService *provider) APPLE_KEXT_OVERRIDE;
    virtual void     free(void) APPLE_KEXT_OVERRIDE;

    virtual IOReturn newUserClient(task_t owningTask, void *securityID,
                                   UInt32 type, OSDictionary *properties,
                                   IOUserClient **handler) APPLE_KEXT_OVERRIDE;

    /* ---- 底层 ACPI ---- */
    int  evalRaw(IOACPIPlatformDevice *dev, UInt8 com, UInt8 sub, UInt8 a1, UInt8 a2);
    int  runMain(int ctlIndex, UInt8 a1 = 0, UInt8 a2 = 0);
    int  rawCall(UInt8 com, UInt8 sub, UInt8 a1, UInt8 a2);

    /* ---- 上层操作（对应 Control::*）---- */
    int  doProbe(void);
    int  probeReply(void);
    int  getFanRPM(int i);
    int  getFanPercent(int i);
    int  getFanBoost(int i);
    int  getMaxRPM(int i);
    int  getTempValue(int sensorIndex);
    int  setFanBoost(int i, int value);
    int  getPowerRaw(void);
    int  getPowerIndex(void);
    int  setPower(int level);
    int  getGMode(void);
    int  setGMode(int on);

    /* ---- 状态查询 ---- */
    int  fanCount(void)    const { return fFanCount; }
    int  fanId(int i)      const { return (i >= 0 && i < fFanCount) ? fFanIds[i] : -1; }
    int  sensorCount(void) const { return fSensorCount; }
    int  powerCount(void)  const { return fPowerCount; }
    int  powerAt(int i)    const { return (i >= 0 && i < fPowerCount) ? fPowers[i] : -1; }
    int  systemId(void)    const { return fSystemId; }
    int  deviceIndex(void) const { return fDeviceIndex; }
    bool isReady(void)     const { return fReady; }
    bool hasGMode(void)    const { return fHasGmode; }

private:
    IOACPIPlatformDevice *findDeviceByName(const char *name);
    bool  probeCurrent(void);
    bool  probeOneDevice(int index);
    void  refreshProperties(void);
    void  applyBootProfile(void);
    void  onTimer(void);

    IOACPIPlatformDevice *fAcpi;
    IOWorkLoop           *fWorkLoop;
    IOTimerEventSource   *fTimer;

    UInt8 fDelta;
    int   fDeviceIndex;
    int   fSystemId;

    UInt8 fFanIds[ALIENFAN_MAX_FANS];
    int   fFanSensor[ALIENFAN_MAX_FANS];   /* 风扇对应的温度传感器下标，-1 = 无 */
    int   fFanCount;

    UInt8 fSensorIdx[ALIENFAN_MAX_SENSORS];
    int   fSensorCount;

    UInt8 fPowers[ALIENFAN_MAX_POWERS];
    int   fPowerCount;

    UInt8 fLastPower;
    bool  fHasGmode;
    bool  fReady;

    /* 配置项（可由 Info.plist 覆盖） */
    bool  fAllowWrite;
    bool  fTryDelta4;       /* 是否尝试 Aurora/Area51 的 delta=4 设备（默认关） */
    int   fBootBoost;       /* 开机自动下发 boost 百分比，0 = 不动 */
    int   fBootMode;        /* 开机自动下发散热模式索引，-1 = 不动 */
    int   fReapplyMs;

    int   fProbeRetry;
};

#endif /* _ALIENFAN_HPP */
