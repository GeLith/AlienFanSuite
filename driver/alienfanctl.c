/*
 * alienfanctl - 控制 AlienFan.kext
 *
 *   alienfanctl info              设备 / 系统 ID / 风扇 / 传感器 / 模式表
 *   alienfanctl status            概览 + 全部读数 + 温度
 *   alienfanctl list              风扇转速 / 百分比 / boost / 最大 RPM
 *   alienfanctl sensors           温度传感器
 *   alienfanctl powers            散热模式列表（索引 -> 原始值）
 *   alienfanctl mode <索引|unlock|gmode>
 *   alienfanctl boost <fan|all> <0-100>
 *   alienfanctl gmode <0|1>
 *   alienfanctl raw <com> <sub> [a1] [a2]     直接下发一条 WMAX，试探协议
 *   alienfanctl dump [--write]                扫描读类子命令，摸出协议全貌
 *   alienfanctl reprobe                       重新探测设备
 *
 * 与 alien-tools 的对应：
 *   setfans  -> boost      （boost 是“在 BIOS 曲线之上加速的百分比”，0 = 不改）
 *   setpower -> mode       （参数是 powers[] 的下标，不是原始模式码）
 *   dump     -> dump
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>

/* 必须与 AlienFan.hpp 中的 kAlienFan* 枚举一致 */
enum {
    kProbe = 0,
    kInfo,
    kGetFan,
    kFanCount,
    kFanId,
    kSetBoost,
    kGetPower,
    kSetPower,
    kGetGMode,
    kSetGMode,
    kRaw,
    kSensorCount,
    kSensor,
    kPowerList
};

#define ERR_NOT_PERMITTED 0xe00002c1
#define ERR_BAD_ARGUMENT  0xe00002bc
#define ERR_INVALID       0xe0000001

static io_connect_t gConn = IO_OBJECT_NULL;
static io_service_t gSvc  = IO_OBJECT_NULL;
static int          gInstances = 0;

/* 找一个 AlienFan 服务实例：wantReady 时只接受 Ready=true 的 */
static io_service_t findService(int wantReady)
{
    CFMutableDictionaryRef m = IOServiceMatching("AlienFan");
    io_iterator_t          it = IO_OBJECT_NULL;
    io_service_t           found = IO_OBJECT_NULL;

    gInstances = 0;
    if (!m || IOServiceGetMatchingServices(kIOMainPortDefault, m, &it) != KERN_SUCCESS)
        return IO_OBJECT_NULL;

    io_service_t svc;
    while ((svc = IOIteratorNext(it)) != IO_OBJECT_NULL) {
        gInstances++;
        Boolean ready = false;
        CFTypeRef v = IORegistryEntryCreateCFProperty(svc, CFSTR("Ready"),
                                                      kCFAllocatorDefault, 0);
        if (v) {
            if (CFGetTypeID(v) == CFBooleanGetTypeID())
                ready = CFBooleanGetValue((CFBooleanRef)v);
            CFRelease(v);
        }
        if (!wantReady || ready) {
            found = svc;
            break;
        }
        IOObjectRelease(svc);
    }
    IOObjectRelease(it);
    return found;
}

static int openDriver(void)
{
    gSvc = findService(1);
    if (!gSvc)
        gSvc = findService(0);          /* 没有就绪的，也打开未就绪的看状态 */

    if (!gSvc) {
        if (gInstances == 0)
            fprintf(stderr, "找不到 AlienFan 服务：kext 没加载，或 personality 没匹配上\n"
                            "  sudo kmutil load -p /Library/Extensions/AlienFan.kext\n");
        else
            fprintf(stderr, "有 %d 个实例但都打不开\n", gInstances);
        return -1;
    }

    kern_return_t kr = IOServiceOpen(gSvc, mach_task_self(), 0, &gConn);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "IOServiceOpen 失败: 0x%x\n", kr);
        IOObjectRelease(gSvc);
        gSvc = IO_OBJECT_NULL;
        return -1;
    }
    return 0;
}

/* 只读：直接打印 IORegistry 属性和内核诊断，不需要 root */
static void dumpProps(void)
{
    CFMutableDictionaryRef m = IOServiceMatching("AlienFan");
    io_iterator_t          it = IO_OBJECT_NULL;
    if (!m || IOServiceGetMatchingServices(kIOMainPortDefault, m, &it) != KERN_SUCCESS) {
        printf("没有 AlienFan 实例\n");
        return;
    }

    const char *keys[] = { "Role", "Diag", "Ready", "Device", "Delta", "SystemId",
                           "ProbeReply", "FanCount", "SensorCount", "PowerCount",
                           "GModeSupported", "PowerRaw", "GMode", NULL };
    io_service_t svc;
    int idx = 0;

    while ((svc = IOIteratorNext(it)) != IO_OBJECT_NULL) {
        printf("--- 实例 %d ---\n", ++idx);
        for (int i = 0; keys[i]; i++) {
            CFStringRef key = CFStringCreateWithCString(kCFAllocatorDefault, keys[i],
                                                        kCFStringEncodingUTF8);
            if (!key)
                continue;
            CFTypeRef v = IORegistryEntryCreateCFProperty(svc, key,
                                                          kCFAllocatorDefault, 0);
            CFRelease(key);
            if (!v)
                continue;
            printf("  %-14s = ", keys[i]);
            CFTypeID t = CFGetTypeID(v);
            if (t == CFStringGetTypeID()) {
                char buf[256];
                if (CFStringGetCString((CFStringRef)v, buf, sizeof(buf), kCFStringEncodingUTF8))
                    printf("%s", buf);
            } else if (t == CFNumberGetTypeID()) {
                long n = 0;
                CFNumberGetValue((CFNumberRef)v, kCFNumberLongType, &n);
                printf("%ld", n);
            } else if (t == CFBooleanGetTypeID()) {
                printf("%s", CFBooleanGetValue((CFBooleanRef)v) ? "true" : "false");
            } else {
                printf("<其他类型>");
            }
            printf("\n");
            CFRelease(v);
        }
        IOObjectRelease(svc);
    }
    IOObjectRelease(it);
    if (!idx)
        printf("没有 AlienFan 实例（kext 未加载或 personality 未匹配）\n");
}

static void closeDriver(void)
{
    if (gConn) { IOServiceClose(gConn); gConn = IO_OBJECT_NULL; }
    if (gSvc)  { IOObjectRelease(gSvc); gSvc = IO_OBJECT_NULL; }
}

static kern_return_t call(uint32_t sel, const uint64_t *in, uint32_t inCnt,
                          uint64_t *out, uint32_t outCnt)
{
    return IOConnectCallScalarMethod(gConn, sel, in, inCnt, out, &outCnt);
}

static void explain(kern_return_t kr)
{
    switch (kr) {
        case ERR_NOT_PERMITTED: fprintf(stderr, "  （驱动是只读模式：AlienFanAllowWrite=false）\n"); break;
        case ERR_BAD_ARGUMENT:  fprintf(stderr, "  （参数越界：风扇索引或模式索引不对）\n"); break;
        case ERR_INVALID:       fprintf(stderr, "  （数值越界：boost 必须 0-100）\n"); break;
        default: break;
    }
}

/* ---------------- 查询 ---------------- */

struct info_t {
    int ready, device, systemId, fans, sensors, powers, hasGmode, gmode;
};

static int getInfo(struct info_t *o)
{
    uint64_t out[8] = {0};
    if (call(kInfo, NULL, 0, out, 8) != KERN_SUCCESS)
        return -1;
    o->ready    = (int)out[0];
    o->device   = (int)(int32_t)out[1];
    o->systemId = (int)(int32_t)out[2];
    o->fans     = (int)out[3];
    o->sensors  = (int)out[4];
    o->powers   = (int)out[5];
    o->hasGmode = (int)out[6];
    o->gmode    = (int)out[7];
    return 0;
}

static const char *devName(int idx)
{
    switch (idx) {
        case 0:  return "AMW1 (Alienware m15/m17)";
        case 1:  return "AMW3 (Dell G15)";
        case 2:  return "AMWW (G5 SE / m15 R6/R7)";
        case 3:  return "AMW1 delta=4 (Aurora R7)";
        case 4:  return "WMI2 delta=4 (Area 51 R4)";
        case -2: return "provider（personality 直接匹配）";
        default: return "未探测到";
    }
}

static int cmdInfo(void)
{
    struct info_t i;
    if (getInfo(&i) != 0) {
        fprintf(stderr, "读取驱动信息失败\n");
        return 1;
    }
    printf("就绪:      %s\n", i.ready ? "是" : "否");
    printf("ACPI 设备: %s\n", devName(i.device));
    printf("系统 ID:   %d\n", i.systemId);
    printf("风扇:      %d\n", i.fans);
    printf("传感器:    %d\n", i.sensors);
    printf("散热模式:  %d 项\n", i.powers);
    printf("G 模式:    %s\n", i.hasGmode ? (i.gmode ? "支持，当前已开" : "支持，当前关闭") : "不支持");
    return i.ready ? 0 : 1;
}

static int cmdList(void)
{
    struct info_t i;
    if (getInfo(&i) != 0) return 1;

    if (i.fans <= 0) {
        printf("没有枚举到风扇。先跑 `alienfanctl dump` 看协议是否对得上。\n");
        return 1;
    }

    printf("  #   id    RPM   %%  boost  maxRPM\n");
    for (int f = 0; f < i.fans; f++) {
        uint64_t in[1] = { (uint64_t)f };
        uint64_t out[4] = {0, 0, 0, 0};
        uint64_t id = 0;

        if (call(kFanId, in, 1, &id, 1) != KERN_SUCCESS)
            id = 0;

        if (call(kGetFan, in, 1, out, 4) != KERN_SUCCESS) {
            printf("  %d  %3llu   读取失败\n", f, id);
            continue;
        }
        printf("  %d  %3llu  %5d  %3d  %5d  %6d\n",
               f, id, (int)(int32_t)out[0], (int)(int32_t)out[1],
               (int)(int32_t)out[2], (int)(int32_t)out[3]);
    }
    return 0;
}

static int cmdSensors(void)
{
    struct info_t i;
    if (getInfo(&i) != 0) return 1;

    if (i.sensors <= 0) {
        printf("没有温度传感器。\n");
        return 0;
    }
    for (int s = 0; s < i.sensors; s++) {
        uint64_t in[1] = { (uint64_t)s }, out[1] = {0};
        if (call(kSensor, in, 1, out, 1) == KERN_SUCCESS)
            printf("  传感器 %d: %d°C\n", s, (int)(int32_t)out[0]);
        else
            printf("  传感器 %d: 读取失败\n", s);
    }
    return 0;
}

static int cmdPowers(void)
{
    struct info_t i;
    if (getInfo(&i) != 0) return 1;

    printf("散热模式表（下标 -> 原始值）:\n");
    for (int p = 0; p < i.powers; p++) {
        uint64_t in[1] = { (uint64_t)p }, out[1] = {0};
        if (call(kPowerList, in, 1, out, 1) != KERN_SUCCESS)
            continue;
        int raw = (int)(uint8_t)out[0];
        printf("  [%d] -> 0x%02x%s\n", p, raw,
               p == 0 ? "  （unlock，手动控制）" : "");
    }

    uint64_t out[2] = {0, 0};
    if (call(kGetPower, NULL, 0, out, 2) == KERN_SUCCESS)
        printf("当前: 原始 0x%02x，下标 %d\n",
               (int)(uint8_t)out[0], (int)(int32_t)out[1]);

    printf("\n提示：`mode 1` .. `mode %d` 依次是上面的档位；"
           "`mode gmode` 是 G 模式（原始 0xab）。\n", i.powers - 1);
    return 0;
}

static int cmdStatus(void)
{
    cmdInfo();
    printf("\n--- 风扇 ---\n");
    cmdList();
    printf("\n--- 温度 ---\n");
    cmdSensors();
    printf("\n--- 散热模式 ---\n");
    cmdPowers();
    return 0;
}

/* ---------------- 写入 ---------------- */

static int cmdBoost(const char *fanArg, const char *valArg)
{
    struct info_t i;
    if (getInfo(&i) != 0) return 1;
    if (i.fans <= 0) {
        fprintf(stderr, "没有可用风扇\n");
        return 1;
    }

    int v = atoi(valArg);
    if (v < 0 || v > 100) {
        fprintf(stderr, "boost 必须 0-100（0 = 保持 BIOS 原策略）\n");
        return 1;
    }

    int failed = 0;
    if (!strcasecmp(fanArg, "all")) {
        for (int f = 0; f < i.fans; f++) {
            uint64_t in[2] = { (uint64_t)f, (uint64_t)v };
            kern_return_t kr = call(kSetBoost, in, 2, NULL, 0);
            if (kr != KERN_SUCCESS) {
                fprintf(stderr, "fan%d 失败: 0x%x\n", f, kr);
                explain(kr);
                failed++;
            } else {
                printf("fan%d -> boost %d%%\n", f, v);
            }
        }
        return failed ? 1 : 0;
    }

    int idx = atoi(fanArg);
    uint64_t in[2] = { (uint64_t)idx, (uint64_t)v };
    kern_return_t kr = call(kSetBoost, in, 2, NULL, 0);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "设置失败: 0x%x\n", kr);
        explain(kr);
        return 1;
    }
    printf("fan%d -> boost %d%%\n", idx, v);
    return 0;
}

static int cmdMode(const char *arg)
{
    int level;

    if (!strcasecmp(arg, "unlock"))      level = 0;
    else if (!strcasecmp(arg, "gmode"))  level = 0xAB;
    else {
        int n = atoi(arg);
        level = n;

        /* 小数（1..powers-1）当下标，查表换成 powers[] 里的原始码。
         * ACPI 只认原始码：直接下发 3 会被拒（0xe00002bc）。
         * 大于 0xAB 或查表失败的，原样透传，方便手工下发原始码。 */
        struct info_t i;
        if (n > 0 && n < 0xAB && getInfo(&i) == 0 && n < i.powers) {
            uint64_t in[1] = { (uint64_t)n }, out[1] = {0};
            if (call(kPowerList, in, 1, out, 1) == KERN_SUCCESS)
                level = (int)(uint8_t)out[0];
        }
    }

    uint64_t in[1] = { (uint64_t)level };
    kern_return_t kr = call(kSetPower, in, 1, NULL, 0);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "设置失败: 0x%x\n", kr);
        explain(kr);
        return 1;
    }
    printf("已下发散热模式 %d (0x%02x)\n", level, level);
    if (level == 0)
        printf("提示：unlock 模式会清掉 boost，BIOS 基线也更低。\n");
    return 0;
}

static int cmdGMode(const char *arg)
{
    int on = atoi(arg) ? 1 : 0;
    uint64_t in[1] = { (uint64_t)on };
    kern_return_t kr = call(kSetGMode, in, 1, NULL, 0);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "设置失败: 0x%x\n", kr);
        explain(kr);
        return 1;
    }
    printf("G 模式 -> %s\n", on ? "开" : "关");
    return 0;
}

/* ---------------- 现场摸协议 ---------------- */

static int cmdRaw(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "用法: alienfanctl raw <com> <sub> [a1] [a2]   例如 raw 0x14 3 0 0\n");
        return 1;
    }
    unsigned com = (unsigned)strtoul(argv[2], NULL, 0);
    unsigned sub = (unsigned)strtoul(argv[3], NULL, 0);
    unsigned a1  = argc > 4 ? (unsigned)strtoul(argv[4], NULL, 0) : 0;
    unsigned a2  = argc > 5 ? (unsigned)strtoul(argv[5], NULL, 0) : 0;

    uint64_t in[4] = { com & 0xff, sub & 0xff, a1 & 0xff, a2 & 0xff };
    uint64_t out[1] = {0};

    kern_return_t kr = call(kRaw, in, 4, out, 1);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "调用失败: 0x%x\n", kr);
        return 1;
    }
    int r = (int)(int32_t)out[0];
    printf("WMAX(0, 0x%02x, {%u,%u,%u,0}) = %d   (0x%02x)\n",
           com & 0xff, sub & 0xff, a1 & 0xff, a2 & 0xff, r, (unsigned char)r);
    return 0;
}

static int cmdDump(int argc, char **argv)
{
    int withWrite = 0;
    for (int i = 2; i < argc; i++)
        if (!strcmp(argv[i], "--write")) withWrite = 1;

    /* 0x15 是写类主命令，默认不碰 */
    static const unsigned comsRead[]  = { 0x14, 0x13, 0x25, 0x1a };
    static const unsigned comsWrite[] = { 0x15 };

    printf("扫描 WMAX(0, com, {sub,0,0,0}) …\n");
    printf("（0x14 = 读，0x13/0x25/0x1a = 信息类，0x15 = 写%s）\n\n",
           withWrite ? "，本次包含" : "，本次跳过，加 --write 才扫");

    for (int pass = 0; pass < (withWrite ? 2 : 1); pass++) {
        const unsigned *coms = pass == 0 ? comsRead : comsWrite;
        int ncoms = pass == 0 ? 4 : 1;

        for (int c = 0; c < ncoms; c++) {
            printf("com 0x%02x:\n", coms[c]);
            for (unsigned sub = 0; sub <= 0x20; sub++) {
                uint64_t in[4] = { coms[c], sub, 0, 0 };
                uint64_t out[1] = {0};
                if (call(kRaw, in, 4, out, 1) != KERN_SUCCESS)
                    continue;
                int r = (int)(int32_t)out[0];
                if (r < 0)
                    continue;
                printf("  sub 0x%02x -> %d\n", sub, r);
            }
        }
    }
    printf("\n读不出来的组合不会列出。若整张表都是空的，说明 WMAX 参数形状不对。\n");
    return 0;
}

static int cmdReprobe(void)
{
    uint64_t out[1] = {0};
    if (call(kProbe, NULL, 0, out, 1) != KERN_SUCCESS || !out[0]) {
        fprintf(stderr, "重新探测失败：没有任何设备应答\n");
        return 1;
    }
    printf("探测成功\n");
    return cmdInfo();
}

static void usage(void)
{
    printf(
        "用法:\n"
        "  alienfanctl info                       设备 / 系统 ID / 各项计数\n"
        "  alienfanctl status                     全量状态\n"
        "  alienfanctl list                       风扇读数\n"
        "  alienfanctl sensors                    温度\n"
        "  alienfanctl powers                     散热模式表\n"
        "  alienfanctl mode <索引|unlock|gmode>   设置散热模式\n"
        "  alienfanctl boost <fan|all> <0-100>    设置 boost 百分比\n"
        "  alienfanctl gmode <0|1>                G 模式开关\n"
        "  alienfanctl raw <com> <sub> [a1] [a2]  直接下发一条 WMAX\n"
        "  alienfanctl dump [--write]             扫描子命令，摸出协议\n"
        "  alienfanctl reprobe                    重新探测\n"
        "  alienfanctl diag                       读注册表诊断（不需要 root）\n"
        "\n注意：boost 是「在 BIOS 曲线之上额外加速的百分比」，0 = 不改，不是绝对转速。\n"
        "      设置非零散热模式通常会禁用手动 boost，两者用其一。\n"
        "\n典型流程:\n"
        "  sudo alienfanctl info\n"
        "  sudo alienfanctl mode unlock        # 先解锁\n"
        "  sudo alienfanctl boost all 100      # 风扇拉满\n"
        "  sudo alienfanctl boost all 0        # 恢复 BIOS 策略\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage();
        return 0;
    }

    /* diag 只读注册表属性，不需要打开服务和 root */
    if (!strcmp(argv[1], "diag")) {
        dumpProps();
        return 0;
    }

    if (openDriver() != 0)
        return 2;

    int rc = 1;
    const char *cmd = argv[1];

    if      (!strcmp(cmd, "info"))     rc = cmdInfo();
    else if (!strcmp(cmd, "status"))   rc = cmdStatus();
    else if (!strcmp(cmd, "list"))     rc = cmdList();
    else if (!strcmp(cmd, "sensors"))  rc = cmdSensors();
    else if (!strcmp(cmd, "powers"))   rc = cmdPowers();
    else if (!strcmp(cmd, "mode")   && argc >= 3) rc = cmdMode(argv[2]);
    else if (!strcmp(cmd, "boost")  && argc >= 4) rc = cmdBoost(argv[2], argv[3]);
    else if (!strcmp(cmd, "gmode")  && argc >= 3) rc = cmdGMode(argv[2]);
    else if (!strcmp(cmd, "raw")          ) rc = cmdRaw(argc, argv);
    else if (!strcmp(cmd, "dump")         ) rc = cmdDump(argc, argv);
    else if (!strcmp(cmd, "reprobe")      ) rc = cmdReprobe();
    else usage();

    closeDriver();
    return rc;
}
