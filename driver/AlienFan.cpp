/*
 * AlienFan.cpp - Alienware / Dell 风扇控制 kext（ACPI 通道）
 *
 * 移植自 alien-tools (T-Troll) v1 SDK 的 ACPI 分支：
 *     alienfan-SDK/alienfan-SDK.cpp   -> Control::*
 *     alienfan-SDK/alienfan-controls.h -> devs[] / dev_controls[]
 *
 * 保持与上游一致的部分：
 *   - WMAX(int 0, int com, buffer[4]{sub, arg1, arg2, 0}) 的调用形状
 *   - 设备逐条探测、谁应答用谁（不靠名字猜）
 *   - 风扇/传感器/散热模式三段式枚举及其终止条件（含 R7 的 0x132 特例）
 *   - delta 偏移、unlock 语义、G 模式 0xAB
 *
 * 有意不同的部分：
 *   - 上游在 SetGMode 里会去 eval \_SB.PCI0.LPC0.EC0._Q14 探路，
 *     直接求值 EC 查询方法风险偏高，这里改为只在 SetGMode 失败时回退 0xAB。
 *   - 不实现 EC 直读温度（SEN0 / ECDV / KDRT），只保留 AWCC 温度通道。
 */

#include "AlienFan.hpp"
#include <IOKit/IOLib.h>
#include <mach/kmod.h>

#define LOG_PREFIX "AlienFan: "

/* ------------------------------------------------------------------ */
/* kmod_info                                                          */
/*                                                                    */
/* 内核集合加载器要求 kext 二进制里存在 _kmod_info 符号（位于          */
/* __DATA,__data）。链接器不会自动生成它，必须由源码显式定义。         */
/* 缺了它的表现就是：                                                 */
/*   cannot be placed in kernel collection because:                   */
/*   kexts must have a _kmod_info symbol                              */
/*                                                                    */
/* IOKit 的生命周期走 IOService::start/stop，这里的 kmod start/stop    */
/* 不会被调用，给 KERN_SUCCESS 桩即可。                               */
/* ------------------------------------------------------------------ */
extern "C" {

kern_return_t AlienFan_kmod_start(kmod_info_t *, void *)
{
    return KERN_SUCCESS;
}

kern_return_t AlienFan_kmod_stop(kmod_info_t *, void *)
{
    return KERN_SUCCESS;
}

}   /* extern "C" */

KMOD_EXPLICIT_DECL(com.dongdong.AlienFan, "2.0.0",
                   AlienFan_kmod_start, AlienFan_kmod_stop)

/* ------------------------------------------------------------------ */
/* 设备表：顺序与 devs[] 完全一致                                      */
/* ------------------------------------------------------------------ */
static const AFDevice kDevices[ALIENFAN_DEVICE_COUNT] = {
    /* Alienware m15/m17 */
    { "AMW1", "\\_SB.AMW1.WMAX", "\\_SB.PCI0.PEG0.PEGP.GPS", 0 },
    /* Dell G15 */
    { "AMW3", "\\_SB.AMW3.WMAX", "\\_SB.PCI0.GPP0.PEGP.GPS", 0 },
    /* Dell G5 SE / Alienware m15 R6 */
    { "AMWW", "\\_SB.AMWW.WMAX", "\\_SB.PC00.PEG1.PEGP.GPS", 0 },
    /* Aurora R7（delta = 4，默认不试） */
    { "AMW1", "\\_SB.AMW1.WMAX", "\\_SB.PCI0.PEG0.PEGP.GPS", 4 },
    /* Area 51 R4（delta = 4，默认不试） */
    { "WMI2", "\\_SB.WMI2.WMAX", "\\_SB.PCI0.PEG0.PEGP.GPS", 4 },
};

/* ------------------------------------------------------------------ */
/* 控制表：顺序与 dev_controls 完全一致                                */
/* ------------------------------------------------------------------ */
static const AFCommand kControls[kCtlCount] = {
    { 0x14, 0x03 },   /* getPowerID    */
    { 0x14, 0x05 },   /* getFanRPM     */
    { 0x14, 0x06 },   /* getFanPercent */
    { 0x14, 0x0c },   /* getFanBoost   */
    { 0x15, 0x02 },   /* setFanBoost   */
    { 0x14, 0x04 },   /* getTemp       */
    { 0x14, 0x0b },   /* getPower      */
    { 0x15, 0x01 },   /* setPower      */
    { 0x13, 0x04 },   /* setGPUPower   */
    { 0x25, 0x02 },   /* getGMode      */
    { 0x25, 0x01 },   /* setGMode      */
    { 0x1a, 0x02 },   /* getSystemID   */
    { 0x13, 0x02 },   /* getFanType    */
    { 0x14, 0x09 },   /* getMaxRPM     */
};

#define PROBE_COM  0x14
#define PROBE_SUB  0x02

/* ================================================================== */
/* 用户态客户端                                                        */
/* ================================================================== */
class AlienFanUserClient : public IOUserClient
{
    OSDeclareDefaultStructors(AlienFanUserClient)

public:
    virtual bool     initWithTask(task_t owningTask, void *securityID,
                                  UInt32 type, OSDictionary *properties) APPLE_KEXT_OVERRIDE;
    virtual bool     start(IOService *provider) APPLE_KEXT_OVERRIDE;
    virtual void     stop(IOService *provider) APPLE_KEXT_OVERRIDE;
    virtual IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
    virtual IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *arguments,
                                    IOExternalMethodDispatch *dispatch, OSObject *target,
                                    void *reference) APPLE_KEXT_OVERRIDE;

    /* 静态入口必须公开：dispatch 表要取地址 */
    static IOReturn sProbe(OSObject *t, void *r, IOExternalMethodArguments *a);
    static IOReturn sInfo(OSObject *t, void *r, IOExternalMethodArguments *a);
    static IOReturn sGetFan(OSObject *t, void *r, IOExternalMethodArguments *a);
    static IOReturn sFanCount(OSObject *t, void *r, IOExternalMethodArguments *a);
    static IOReturn sFanId(OSObject *t, void *r, IOExternalMethodArguments *a);
    static IOReturn sSetBoost(OSObject *t, void *r, IOExternalMethodArguments *a);
    static IOReturn sGetPower(OSObject *t, void *r, IOExternalMethodArguments *a);
    static IOReturn sSetPower(OSObject *t, void *r, IOExternalMethodArguments *a);
    static IOReturn sGetGMode(OSObject *t, void *r, IOExternalMethodArguments *a);
    static IOReturn sSetGMode(OSObject *t, void *r, IOExternalMethodArguments *a);
    static IOReturn sRaw(OSObject *t, void *r, IOExternalMethodArguments *a);
    static IOReturn sSensorCount(OSObject *t, void *r, IOExternalMethodArguments *a);
    static IOReturn sSensor(OSObject *t, void *r, IOExternalMethodArguments *a);
    static IOReturn sPowerList(OSObject *t, void *r, IOExternalMethodArguments *a);

private:
    AlienFan *fOwner;
};

/* 顺序必须与 kAlienFan* 枚举一致 */
static const IOExternalMethodDispatch gDispatch[kAlienFanMethodCount] = {
    { &AlienFanUserClient::sProbe,      0, 0, 1, 0 },   /* probe       */
    { &AlienFanUserClient::sInfo,       0, 0, 8, 0 },   /* info        */
    { &AlienFanUserClient::sGetFan,     1, 0, 4, 0 },   /* getFan      */
    { &AlienFanUserClient::sFanCount,   0, 0, 1, 0 },   /* fanCount    */
    { &AlienFanUserClient::sFanId,      1, 0, 1, 0 },   /* fanId       */
    { &AlienFanUserClient::sSetBoost,   2, 0, 0, 0 },   /* setBoost    */
    { &AlienFanUserClient::sGetPower,   0, 0, 2, 0 },   /* getPower    */
    { &AlienFanUserClient::sSetPower,   1, 0, 0, 0 },   /* setPower    */
    { &AlienFanUserClient::sGetGMode,   0, 0, 1, 0 },   /* getGMode    */
    { &AlienFanUserClient::sSetGMode,   1, 0, 0, 0 },   /* setGMode    */
    { &AlienFanUserClient::sRaw,        4, 0, 1, 0 },   /* raw         */
    { &AlienFanUserClient::sSensorCount,0, 0, 1, 0 },   /* sensorCount */
    { &AlienFanUserClient::sSensor,     1, 0, 1, 0 },   /* sensor      */
    { &AlienFanUserClient::sPowerList,  1, 0, 1, 0 },   /* powerList   */
};

/* ================================================================== */
/* AlienFan                                                            */
/* ================================================================== */

#define super IOService
OSDefineMetaClassAndStructors(AlienFan, IOService)

bool AlienFan::init(OSDictionary *dict)
{
    if (!super::init(dict))
        return false;

    fAcpi        = NULL;
    fWorkLoop    = NULL;
    fTimer       = NULL;
    fDelta       = 0;
    fDeviceIndex = -1;
    fSystemId    = -1;
    fFanCount    = 0;
    fSensorCount = 0;
    fPowerCount  = 0;
    fLastPower   = 0;
    fHasGmode    = false;
    fReady       = false;
    fProbeRetry  = 0;

    for (int i = 0; i < ALIENFAN_MAX_FANS; i++) {
        fFanIds[i]    = 0;
        fFanSensor[i] = -1;
    }
    for (int i = 0; i < ALIENFAN_MAX_SENSORS; i++)
        fSensorIdx[i] = 0;
    for (int i = 0; i < ALIENFAN_MAX_POWERS; i++)
        fPowers[i] = 0;

    /* 默认配置 */
    fAllowWrite = true;
    fTryDelta4  = false;   /* 非笔记本机型才开，避免无意义的 0x10/0x11 调用 */
    fBootBoost  = 0;
    fBootMode   = -1;
    fReapplyMs  = 0;

    if (dict) {
        if (OSNumber *n = OSDynamicCast(OSNumber, dict->getObject("AlienFanBootBoost")))
            fBootBoost = (int)n->unsigned32BitValue();
        if (OSNumber *n = OSDynamicCast(OSNumber, dict->getObject("AlienFanBootMode")))
            fBootMode = (int)n->unsigned32BitValue();
        if (OSNumber *n = OSDynamicCast(OSNumber, dict->getObject("AlienFanReapplyMs")))
            fReapplyMs = (int)n->unsigned32BitValue();
        if (OSBoolean *b = OSDynamicCast(OSBoolean, dict->getObject("AlienFanAllowWrite")))
            fAllowWrite = (b == kOSBooleanTrue);
        if (OSBoolean *b = OSDynamicCast(OSBoolean, dict->getObject("AlienFanTryDelta4")))
            fTryDelta4 = (b == kOSBooleanTrue);
    }

    return true;
}

bool AlienFan::start(IOService *provider)
{
    if (!super::start(provider))
        return false;

    fWorkLoop = IOWorkLoop::workLoop();
    if (!fWorkLoop) {
        IOLog(LOG_PREFIX "无法创建 workloop\n");
        return false;
    }

    fTimer = IOTimerEventSource::timerEventSource(this,
                OSMemberFunctionCast(IOTimerEventSource::Action, this, &AlienFan::onTimer));
    if (!fTimer || fWorkLoop->addEventSource(fTimer) != kIOReturnSuccess) {
        IOLog(LOG_PREFIX "无法创建定时器\n");
        OSSafeReleaseNULL(fTimer);
        OSSafeReleaseNULL(fWorkLoop);
        return false;
    }

    /* 备选 personality：provider 本身就是 ACPI 设备（记为 -2） */
    if (IOACPIPlatformDevice *dev = OSDynamicCast(IOACPIPlatformDevice, provider)) {
        dev->retain();
        fAcpi        = dev;
        fDeviceIndex = -2;
        fDelta       = 0;
        setProperty("ACPIObject", "provider");
    }

    onTimer();

    registerService();
    return true;
}

void AlienFan::stop(IOService *provider)
{
    if (fTimer) {
        fTimer->cancelTimeout();
        if (fWorkLoop)
            fWorkLoop->removeEventSource(fTimer);
        OSSafeReleaseNULL(fTimer);
    }
    OSSafeReleaseNULL(fWorkLoop);
    OSSafeReleaseNULL(fAcpi);

    fReady = false;
    super::stop(provider);
}

void AlienFan::free(void)
{
    OSSafeReleaseNULL(fAcpi);
    OSSafeReleaseNULL(fTimer);
    OSSafeReleaseNULL(fWorkLoop);
    super::free();
}

/* ---------------- 底层 ACPI ---------------- */

int AlienFan::evalRaw(IOACPIPlatformDevice *dev, UInt8 com, UInt8 sub, UInt8 a1, UInt8 a2)
{
    if (!dev)
        return -1;

    UInt8 operand[4] = { sub, a1, a2, 0 };

    OSNumber *p0 = OSNumber::withNumber((UInt64)0,   32);
    OSNumber *p1 = OSNumber::withNumber((UInt64)com, 32);
    OSData   *p2 = OSData::withBytes(operand, sizeof(operand));

    int result = -1;

    if (p0 && p1 && p2) {
        OSObject *params[3] = { p0, p1, p2 };
        OSObject *ret = NULL;

        IOReturn kr = dev->evaluateObject("WMAX", &ret, params, 3, 0);
        if (kr == kIOReturnSuccess && ret) {
            if (OSNumber *num = OSDynamicCast(OSNumber, ret)) {
                result = (int)(SInt32)num->unsigned32BitValue();
            } else if (OSData *data = OSDynamicCast(OSData, ret)) {
                if (data->getLength() >= 4) {
                    UInt32 v = 0;
                    memcpy(&v, data->getBytesNoCopy(), 4);
                    result = (int)(SInt32)v;
                }
            }
            ret->release();
        }
    }

    if (p0) p0->release();
    if (p1) p1->release();
    if (p2) p2->release();

    return result;
}

/* 对应 Control::RunMainCommand：com 减去设备 delta */
int AlienFan::runMain(int ctlIndex, UInt8 a1, UInt8 a2)
{
    if (!fAcpi || ctlIndex < 0 || ctlIndex >= kCtlCount)
        return -1;

    const AFCommand &c = kControls[ctlIndex];
    return evalRaw(fAcpi, (UInt8)(c.com - fDelta), c.sub, a1, a2);
}

/* 原始调用：不减 delta，供现场摸协议用 */
int AlienFan::rawCall(UInt8 com, UInt8 sub, UInt8 a1, UInt8 a2)
{
    if (!fAcpi)
        return -1;
    return evalRaw(fAcpi, com, sub, a1, a2);
}

IOACPIPlatformDevice *AlienFan::findDeviceByName(const char *name)
{
    const IORegistryPlane *plane = IORegistryEntry::getPlane("IOACPIPlane");
    if (!plane)
        return NULL;

    IORegistryIterator *iter =
        IORegistryIterator::iterateOver(plane, kIORegistryIterateRecursively);
    if (!iter)
        return NULL;

    IOACPIPlatformDevice *found = NULL;
    IORegistryEntry *entry;

    while ((entry = iter->getNextObject())) {
        const char *n = entry->getName();
        if (!n || strcmp(n, name) != 0)
            continue;

        IOACPIPlatformDevice *dev = OSDynamicCast(IOACPIPlatformDevice, entry);
        if (dev) {
            dev->retain();
            found = dev;
            break;
        }
    }

    iter->release();
    return found;
}

/* ---------------- 探测 ---------------- */

/*
 * 假定 fAcpi / fDelta 已就位，执行与 Control::Probe 相同的枚举序列。
 */
bool AlienFan::probeCurrent(void)
{
    if (!fAcpi)
        return false;

    fFanCount = 0;
    fSensorCount = 0;
    fPowerCount = 0;
    fHasGmode = false;
    fLastPower = 0;
    for (int i = 0; i < ALIENFAN_MAX_FANS; i++)
        fFanSensor[i] = -1;

    int probe = evalRaw(fAcpi, (UInt8)(PROBE_COM - fDelta), PROBE_SUB, 0, 0);
    setProperty("ProbeReply", probe, 32);
    if (probe <= 0)
        return false;

    fSystemId = runMain(kCtlGetSystemID, 2, 0);
    fHasGmode = (runMain(kCtlGetGMode, 0, 0) >= 0);

    /* --- 风扇枚举 ---
     * 上游条件：((id < 0x100 && id > 0) || id > 0x130)
     * 后半段是为 m15 R7 那个 0x132 的风扇留的，别擅自简化。
     */
    int fIndex = 0;
    int funcID = 0;

    while (((funcID = runMain(kCtlGetPowerID, (UInt8)fIndex, 0)) < 0x100 && funcID > 0)
           || funcID > 0x130) {
        if (fFanCount < ALIENFAN_MAX_FANS)
            fFanIds[fFanCount++] = (UInt8)(funcID & 0xff);
        fIndex++;
        if (fIndex > ALIENFAN_SCAN_LIMIT)
            break;
    }

    /* --- 温度传感器枚举 --- */
    const int firstSenIndex = fIndex;
    if (fIndex > 0) {
        int sIndex = 0;
        do {
            sIndex = fIndex - firstSenIndex;
            if (runMain(kCtlGetTemp, (UInt8)funcID, 0) > 0) {
                if (fSensorCount < ALIENFAN_MAX_SENSORS)
                    fSensorIdx[fSensorCount++] = (UInt8)(funcID & 0xff);
            }
            fIndex++;
            if (sIndex > ALIENFAN_SCAN_LIMIT)
                break;
        } while ((funcID = runMain(kCtlGetPowerID, (UInt8)fIndex, 0)) > 0x100
                 && funcID < 0x1A0);
    }

    /* --- 散热模式枚举 --- */
    fPowers[fPowerCount++] = 0;                  /* 0 = unlock（手动控制） */
    if (fDelta == 0 && funcID > 0) {
        do {
            if (fPowerCount < ALIENFAN_MAX_POWERS)
                fPowers[fPowerCount++] = (UInt8)(funcID & 0xff);
            fIndex++;
        } while ((funcID = runMain(kCtlGetPowerID, (UInt8)fIndex, 0)) && funcID > 0);
    } else if (fPowerCount < ALIENFAN_MAX_POWERS) {
        fPowers[fPowerCount++] = 1;
    }

    /* --- 风扇与温度传感器的对应关系 --- */
    for (int i = 0; i < fFanCount; i++) {
        int sid = runMain(kCtlGetFanType, fFanIds[i], 0);
        if (sid < 0)
            continue;
        for (int s = 0; s < fSensorCount; s++) {
            if (fSensorIdx[s] == (UInt8)(sid & 0xff)) {
                fFanSensor[i] = s;
                break;
            }
        }
    }

    setProperty("Device", kDevices[fDeviceIndex >= 0 ? fDeviceIndex : 0].path);
    setProperty("Delta", fDelta, 32);
    setProperty("SystemId", fSystemId, 32);
    setProperty("FanCount", fFanCount, 32);
    setProperty("SensorCount", fSensorCount, 32);
    setProperty("PowerCount", fPowerCount, 32);
    if (fHasGmode)
        setProperty("GModeSupported", kOSBooleanTrue);

    IOLog(LOG_PREFIX "设备 %s 应答（delta=%d），systemID=%d，风扇 %d，传感器 %d，模式 %d%s\n",
          fDeviceIndex >= 0 ? kDevices[fDeviceIndex].path : "provider",
          fDelta, fSystemId, fFanCount, fSensorCount, fPowerCount,
          fHasGmode ? "，支持 G 模式" : "");

    if (fFanCount == 0)
        IOLog(LOG_PREFIX "警告：探测应答但没枚举到风扇\n");

    return true;
}

bool AlienFan::probeOneDevice(int index)
{
    if (index < 0 || index >= ALIENFAN_DEVICE_COUNT)
        return false;

    const AFDevice &cand = kDevices[index];

    /* delta != 0 的条目只给 Aurora / Area51 用，默认跳过 */
    if (cand.delta != 0 && !fTryDelta4)
        return false;

    IOACPIPlatformDevice *dev = findDeviceByName(cand.name);
    if (!dev)
        return false;

    OSSafeReleaseNULL(fAcpi);
    fAcpi        = dev;              /* findDeviceByName 已 retain */
    fDelta       = (UInt8)cand.delta;
    fDeviceIndex = index;

    if (probeCurrent())
        return true;

    OSSafeReleaseNULL(fAcpi);
    fDeviceIndex = -1;
    fDelta = 0;
    return false;
}

int AlienFan::probeReply(void)
{
    if (OSNumber *n = OSDynamicCast(OSNumber, getProperty("ProbeReply")))
        return (int)n->unsigned32BitValue();
    return -999;
}

/* 对应 Control::Probe：逐条探测，谁应答用谁。
 * 每一步的结果都写进 Diag 属性——macOS 26 上 log show 常被沙箱挡住，
 * 靠 ioreg 读属性是唯一稳定的取证通道。 */
int AlienFan::doProbe(void)
{
    char diag[240];
    int  pos = 0;
    diag[0] = 0;

    /* personality 直接给了 ACPI 设备的情况，先试它 */
    if (fAcpi && fDeviceIndex == -2) {
        const char *nm = getName() ? getName() : "?";
        bool ok = probeCurrent();
        pos += snprintf(diag + pos, sizeof(diag) - pos, "%s:%d ", nm, probeReply());

        if (ok) {
            setProperty("Role", "provider");
            setProperty("Diag", diag);
            /* 必须返回非负：-2 是"待探测"的哨兵值，调用方用 < 0 判失败 */
            return 0;
        }
        OSSafeReleaseNULL(fAcpi);
        fDeviceIndex = -1;
    }

    for (int i = 0; i < ALIENFAN_DEVICE_COUNT; i++) {
        if (probeOneDevice(i)) {
            pos += snprintf(diag + pos, sizeof(diag) - pos, "%s:OK", kDevices[i].name);
            setProperty("Role", "scan");
            setProperty("Diag", diag);
            return i;
        }
        if (pos < (int)sizeof(diag) - 24)
            pos += snprintf(diag + pos, sizeof(diag) - pos, "%s:%d ",
                            kDevices[i].name, probeReply());
    }

    setProperty("Role", "none");
    setProperty("Diag", diag);
    setProperty("ProbeReply", -1, 32);
    return -1;
}

/* ---------------- 风扇 / 温度 / 模式 ---------------- */

int AlienFan::getFanRPM(int i)
{
    if (i < 0 || i >= fFanCount) return -1;
    return runMain(kCtlGetFanRPM, fFanIds[i], 0);
}

int AlienFan::getFanPercent(int i)
{
    if (i < 0 || i >= fFanCount) return -1;
    return runMain(kCtlGetFanPercent, fFanIds[i], 0);
}

int AlienFan::getFanBoost(int i)
{
    if (i < 0 || i >= fFanCount) return -1;
    return runMain(kCtlGetFanBoost, fFanIds[i], 0);
}

int AlienFan::getMaxRPM(int i)
{
    if (i < 0 || i >= fFanCount) return -1;
    return runMain(kCtlGetMaxRPM, fFanIds[i], 0);
}

int AlienFan::getTempValue(int sensorIndex)
{
    if (sensorIndex < 0 || sensorIndex >= fSensorCount) return -1;
    return runMain(kCtlGetTemp, fSensorIdx[sensorIndex], 0);
}

/*
 * boost 是“在 BIOS 风扇曲线之上额外加速的百分比”，0 = 保持 BIOS 原策略。
 * 它不会让风扇停转，所以不需要下限保护。
 */
int AlienFan::setFanBoost(int i, int value)
{
    if (!fAllowWrite)                return -2;
    if (i < 0 || i >= fFanCount)     return -3;
    if (value < 0 || value > 100)    return -4;
    return runMain(kCtlSetFanBoost, fFanIds[i], (UInt8)value);
}

int AlienFan::getPowerRaw(void)
{
    return runMain(kCtlGetPower, 0, 0);
}

int AlienFan::getPowerIndex(void)
{
    if (fDelta)
        return fLastPower;

    int pl = getPowerRaw();
    if (pl < 0)
        return -1;

    for (int i = 0; i < fPowerCount; i++)
        if (fPowers[i] == (UInt8)(pl & 0xff))
            return i;
    return -1;
}

/*
 * level 是 powers[] 的下标（0 = unlock），不是原始模式码。
 * 例外：0xAB 是 G 模式，直接透传。
 */
int AlienFan::setPower(int level)
{
    if (!fAllowWrite)                 return -2;
    if (level < 0 || level > 0xAB)    return -3;

    fLastPower = (UInt8)level;
    if (fDelta && level == 0)
        ++level;

    return runMain(kCtlSetPower, (UInt8)level, 0);
}

int AlienFan::getGMode(void)
{
    if (!fHasGmode)
        return 0;

    int pm = getPowerRaw();
    if (pm == 0xab || pm < 0)
        return 1;
    return runMain(kCtlGetGMode, 0, 0) > 0 ? 1 : 0;
}

int AlienFan::setGMode(int on)
{
    if (!fAllowWrite) return -2;
    if (!fHasGmode)   return -3;

    int r = runMain(kCtlSetGMode, on ? 1 : 0, 0);
    if (r < 0 && on) {
        /* 上游的兜底：某些 BIOS 只能靠把散热模式设成 0xAB 进 G 模式 */
        setPower(0xAB);
        return getGMode();
    }
    return r;
}

/* ---------------- 开机策略 / 重试 ---------------- */

void AlienFan::refreshProperties(void)
{
    for (int i = 0; i < fFanCount; i++) {
        char key[32];

        int rpm   = getFanRPM(i);
        int pct   = getFanPercent(i);
        int boost = getFanBoost(i);
        int maxr  = getMaxRPM(i);

        snprintf(key, sizeof(key), "Fan%dRPM", i);
        setProperty(key, rpm, 32);
        snprintf(key, sizeof(key), "Fan%dPercent", i);
        setProperty(key, pct, 32);
        snprintf(key, sizeof(key), "Fan%dBoost", i);
        setProperty(key, boost, 32);
        snprintf(key, sizeof(key), "Fan%dMaxRPM", i);
        setProperty(key, maxr, 32);
    }

    setProperty("PowerRaw", getPowerRaw(), 32);
    setProperty("GMode", getGMode(), 32);
}

void AlienFan::applyBootProfile(void)
{
    if (!fReady || !fAllowWrite)
        return;

    /* boost 直接下发，不强制切 unlock。
     * 实测 m15 R7：BIOS 默认档（0xa0）下 boost 100% 能顶到 4796/4800，
     * 而先 setPower(0) 反而把基线压低、只能到 4615。上游那句
     * "先 unlock 否则禁用手动控制"在这台机器上并不成立。
     * 只有在显式配置了 BootMode 时，才按模式走（模式会清掉 boost）。 */
    if (fBootBoost > 0) {
        for (int i = 0; i < fFanCount; i++) {
            int r = setFanBoost(i, fBootBoost);
            IOLog(LOG_PREFIX "风扇 %d 下发 boost %d%% -> %d\n", i, fBootBoost, r);
        }
    }

    if (fBootMode >= 0) {
        int r = setPower(fBootMode);
        IOLog(LOG_PREFIX "下发散热模式索引 %d -> %d\n", fBootMode, r);
    }
}

void AlienFan::onTimer(void)
{
    /* -2 = provider 直接匹配、尚未探测；-1 = 待扫描。两者都要进探测 */
    if (!fAcpi || fDeviceIndex < 0) {
        if (++fProbeRetry > 20) {
            IOLog(LOG_PREFIX "始终没有设备应答，放弃\n");
            return;
        }

        if (doProbe() < 0) {
            IOLog(LOG_PREFIX "暂无设备应答，1s 后重试（%d）\n", fProbeRetry);
            fTimer->setTimeoutMS(1000);
            return;
        }
    }

    if (!fReady) {
        fReady = true;
        setProperty("Ready", kOSBooleanTrue);
        refreshProperties();
        applyBootProfile();
    } else {
        refreshProperties();
        if (fReapplyMs > 0)
            applyBootProfile();
    }

    if (fReapplyMs > 0)
        fTimer->setTimeoutMS(fReapplyMs);
}

IOReturn AlienFan::newUserClient(task_t owningTask, void *securityID, UInt32 type,
                                 OSDictionary *properties, IOUserClient **handler)
{
    AlienFanUserClient *client = new AlienFanUserClient;
    if (!client)
        return kIOReturnNoMemory;

    if (!client->initWithTask(owningTask, securityID, type, properties) ||
        !client->attach(this) ||
        !client->start(this)) {
        client->detach(this);
        client->release();
        return kIOReturnError;
    }

    *handler = client;
    return kIOReturnSuccess;
}

/* ================================================================== */
/* AlienFanUserClient                                                  */
/* ================================================================== */

#undef super
#define super IOUserClient
OSDefineMetaClassAndStructors(AlienFanUserClient, IOUserClient)

bool AlienFanUserClient::initWithTask(task_t owningTask, void *securityID,
                                      UInt32 type, OSDictionary *properties)
{
    fOwner = NULL;
    if (!super::initWithTask(owningTask, securityID, type, properties))
        return false;
    if (!owningTask)
        return false;
    return true;
}

bool AlienFanUserClient::start(IOService *provider)
{
    if (!super::start(provider))
        return false;

    fOwner = OSDynamicCast(AlienFan, provider);
    if (!fOwner)
        return false;

    return true;
}

void AlienFanUserClient::stop(IOService *provider)
{
    fOwner = NULL;
    super::stop(provider);
}

IOReturn AlienFanUserClient::clientClose(void)
{
    terminate();
    return kIOReturnSuccess;
}

IOReturn AlienFanUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *arguments,
                                            IOExternalMethodDispatch *dispatch, OSObject *target,
                                            void *reference)
{
    if (selector >= kAlienFanMethodCount || !fOwner)
        return kIOReturnBadArgument;

    return super::externalMethod(selector, arguments,
                                 (IOExternalMethodDispatch *)&gDispatch[selector],
                                 this, NULL);
}

/* 把驱动返回的负错误码映射成 IOReturn */
static IOReturn afError(int r)
{
    if (r == -2) return kIOReturnNotPermitted;
    if (r == -3) return kIOReturnBadArgument;
    if (r == -4) return kIOReturnInvalid;
    if (r <  0)  return kIOReturnError;
    return kIOReturnSuccess;
}

IOReturn AlienFanUserClient::sProbe(OSObject *t, void *r, IOExternalMethodArguments *a)
{
    AlienFanUserClient *self = OSDynamicCast(AlienFanUserClient, t);
    if (!self || !self->fOwner)
        return kIOReturnNotAttached;

    a->scalarOutput[0] = (uint64_t)(self->fOwner->doProbe() >= 0 ? 1 : 0);
    return kIOReturnSuccess;
}

IOReturn AlienFanUserClient::sInfo(OSObject *t, void *r, IOExternalMethodArguments *a)
{
    AlienFanUserClient *self = OSDynamicCast(AlienFanUserClient, t);
    if (!self || !self->fOwner)
        return kIOReturnNotAttached;

    AlienFan *o = self->fOwner;

    a->scalarOutput[0] = (uint64_t)(o->isReady() ? 1 : 0);
    a->scalarOutput[1] = (uint64_t)(uint32_t)o->deviceIndex();
    a->scalarOutput[2] = (uint64_t)(uint32_t)o->systemId();
    a->scalarOutput[3] = (uint64_t)(uint32_t)o->fanCount();
    a->scalarOutput[4] = (uint64_t)(uint32_t)o->sensorCount();
    a->scalarOutput[5] = (uint64_t)(uint32_t)o->powerCount();
    a->scalarOutput[6] = (uint64_t)(o->hasGMode() ? 1 : 0);
    a->scalarOutput[7] = (uint64_t)(uint32_t)o->getGMode();

    return kIOReturnSuccess;
}

IOReturn AlienFanUserClient::sGetFan(OSObject *t, void *r, IOExternalMethodArguments *a)
{
    AlienFanUserClient *self = OSDynamicCast(AlienFanUserClient, t);
    if (!self || !self->fOwner)
        return kIOReturnNotAttached;

    int i = (int)a->scalarInput[0];
    if (i < 0 || i >= self->fOwner->fanCount())
        return kIOReturnBadArgument;

    a->scalarOutput[0] = (uint64_t)(uint32_t)self->fOwner->getFanRPM(i);
    a->scalarOutput[1] = (uint64_t)(uint32_t)self->fOwner->getFanPercent(i);
    a->scalarOutput[2] = (uint64_t)(uint32_t)self->fOwner->getFanBoost(i);
    a->scalarOutput[3] = (uint64_t)(uint32_t)self->fOwner->getMaxRPM(i);

    return kIOReturnSuccess;
}

IOReturn AlienFanUserClient::sFanCount(OSObject *t, void *r, IOExternalMethodArguments *a)
{
    AlienFanUserClient *self = OSDynamicCast(AlienFanUserClient, t);
    if (!self || !self->fOwner)
        return kIOReturnNotAttached;

    a->scalarOutput[0] = (uint64_t)(uint32_t)self->fOwner->fanCount();
    return kIOReturnSuccess;
}

IOReturn AlienFanUserClient::sFanId(OSObject *t, void *r, IOExternalMethodArguments *a)
{
    AlienFanUserClient *self = OSDynamicCast(AlienFanUserClient, t);
    if (!self || !self->fOwner)
        return kIOReturnNotAttached;

    int id = self->fOwner->fanId((int)a->scalarInput[0]);
    if (id < 0)
        return kIOReturnBadArgument;

    a->scalarOutput[0] = (uint64_t)(uint32_t)id;
    return kIOReturnSuccess;
}

IOReturn AlienFanUserClient::sSetBoost(OSObject *t, void *r, IOExternalMethodArguments *a)
{
    AlienFanUserClient *self = OSDynamicCast(AlienFanUserClient, t);
    if (!self || !self->fOwner)
        return kIOReturnNotAttached;

    int rc = self->fOwner->setFanBoost((int)a->scalarInput[0], (int)a->scalarInput[1]);
    return afError(rc);
}

IOReturn AlienFanUserClient::sGetPower(OSObject *t, void *r, IOExternalMethodArguments *a)
{
    AlienFanUserClient *self = OSDynamicCast(AlienFanUserClient, t);
    if (!self || !self->fOwner)
        return kIOReturnNotAttached;

    a->scalarOutput[0] = (uint64_t)(uint32_t)self->fOwner->getPowerRaw();
    a->scalarOutput[1] = (uint64_t)(uint32_t)self->fOwner->getPowerIndex();
    return kIOReturnSuccess;
}

IOReturn AlienFanUserClient::sSetPower(OSObject *t, void *r, IOExternalMethodArguments *a)
{
    AlienFanUserClient *self = OSDynamicCast(AlienFanUserClient, t);
    if (!self || !self->fOwner)
        return kIOReturnNotAttached;

    int rc = self->fOwner->setPower((int)a->scalarInput[0]);
    return afError(rc);
}

IOReturn AlienFanUserClient::sGetGMode(OSObject *t, void *r, IOExternalMethodArguments *a)
{
    AlienFanUserClient *self = OSDynamicCast(AlienFanUserClient, t);
    if (!self || !self->fOwner)
        return kIOReturnNotAttached;

    a->scalarOutput[0] = (uint64_t)(uint32_t)self->fOwner->getGMode();
    return kIOReturnSuccess;
}

IOReturn AlienFanUserClient::sSetGMode(OSObject *t, void *r, IOExternalMethodArguments *a)
{
    AlienFanUserClient *self = OSDynamicCast(AlienFanUserClient, t);
    if (!self || !self->fOwner)
        return kIOReturnNotAttached;

    int rc = self->fOwner->setGMode((int)a->scalarInput[0] ? 1 : 0);
    return afError(rc);
}

IOReturn AlienFanUserClient::sRaw(OSObject *t, void *r, IOExternalMethodArguments *a)
{
    AlienFanUserClient *self = OSDynamicCast(AlienFanUserClient, t);
    if (!self || !self->fOwner)
        return kIOReturnNotAttached;

    int res = self->fOwner->rawCall((UInt8)a->scalarInput[0], (UInt8)a->scalarInput[1],
                                    (UInt8)a->scalarInput[2], (UInt8)a->scalarInput[3]);
    a->scalarOutput[0] = (uint64_t)(uint32_t)res;
    return kIOReturnSuccess;
}

IOReturn AlienFanUserClient::sSensorCount(OSObject *t, void *r, IOExternalMethodArguments *a)
{
    AlienFanUserClient *self = OSDynamicCast(AlienFanUserClient, t);
    if (!self || !self->fOwner)
        return kIOReturnNotAttached;

    a->scalarOutput[0] = (uint64_t)(uint32_t)self->fOwner->sensorCount();
    return kIOReturnSuccess;
}

IOReturn AlienFanUserClient::sSensor(OSObject *t, void *r, IOExternalMethodArguments *a)
{
    AlienFanUserClient *self = OSDynamicCast(AlienFanUserClient, t);
    if (!self || !self->fOwner)
        return kIOReturnNotAttached;

    int i = (int)a->scalarInput[0];
    if (i < 0 || i >= self->fOwner->sensorCount())
        return kIOReturnBadArgument;

    a->scalarOutput[0] = (uint64_t)(uint32_t)self->fOwner->getTempValue(i);
    return kIOReturnSuccess;
}

IOReturn AlienFanUserClient::sPowerList(OSObject *t, void *r, IOExternalMethodArguments *a)
{
    AlienFanUserClient *self = OSDynamicCast(AlienFanUserClient, t);
    if (!self || !self->fOwner)
        return kIOReturnNotAttached;

    int i = (int)a->scalarInput[0];
    if (i < 0 || i >= self->fOwner->powerCount())
        return kIOReturnBadArgument;

    a->scalarOutput[0] = (uint64_t)(uint32_t)self->fOwner->powerAt(i);
    return kIOReturnSuccess;
}
