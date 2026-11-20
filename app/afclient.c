#include "afclient.h"

#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

struct AFClient {
    io_connect_t conn;
    io_service_t svc;
};

static char gErr[256] = "";

const char *af_last_error(void) { return gErr; }

static void seterr(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(gErr, sizeof(gErr), fmt, ap);
    va_end(ap);
}

static void copy_str_prop(io_service_t svc, CFStringRef key, char *dst, size_t cap)
{
    if (!svc || !dst || cap == 0) return;
    dst[0] = 0;
    CFTypeRef v = IORegistryEntryCreateCFProperty(svc, key, kCFAllocatorDefault, 0);
    if (!v) return;
    if (CFGetTypeID(v) == CFStringGetTypeID())
        CFStringGetCString((CFStringRef)v, dst, (CFIndex)cap, kCFStringEncodingUTF8);
    CFRelease(v);
}

static int copy_int_prop(io_service_t svc, CFStringRef key, int fallback)
{
    if (!svc) return fallback;
    CFTypeRef v = IORegistryEntryCreateCFProperty(svc, key, kCFAllocatorDefault, 0);
    if (!v) return fallback;
    int value = fallback;
    if (CFGetTypeID(v) == CFNumberGetTypeID())
        CFNumberGetValue((CFNumberRef)v, kCFNumberIntType, &value);
    else if (CFGetTypeID(v) == CFBooleanGetTypeID())
        value = CFBooleanGetValue((CFBooleanRef)v) ? 1 : 0;
    CFRelease(v);
    return value;
}

int af_scan(AFDriverScan *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    out->fanCount = -1;
    out->sensorCount = -1;
    out->powerCount = -1;
    out->systemId = -1;

    CFMutableDictionaryRef m = IOServiceMatching("AlienFan");
    io_iterator_t it = IO_OBJECT_NULL;
    if (!m || IOServiceGetMatchingServices(kIOMainPortDefault, m, &it) != KERN_SUCCESS)
        return -1;

    io_service_t svc;
    while ((svc = IOIteratorNext(it)) != IO_OBJECT_NULL) {
        out->instances++;
        int ready = copy_int_prop(svc, CFSTR("Ready"), 0);
        if (ready) out->readyInstances++;
        /* 优先取 Ready 的实例，其次取第一个实例 */
        if (out->readyInstances && ready) {
            copy_str_prop(svc, CFSTR("Role"), out->role, sizeof(out->role));
            copy_str_prop(svc, CFSTR("Diag"), out->diag, sizeof(out->diag));
            copy_str_prop(svc, CFSTR("Device"), out->device, sizeof(out->device));
            copy_str_prop(svc, CFSTR("CFBundleVersion"), out->bundleVersion,
                          sizeof(out->bundleVersion));
            out->fanCount    = copy_int_prop(svc, CFSTR("FanCount"), -1);
            out->sensorCount = copy_int_prop(svc, CFSTR("SensorCount"), -1);
            out->powerCount  = copy_int_prop(svc, CFSTR("PowerCount"), -1);
            out->systemId    = copy_int_prop(svc, CFSTR("SystemId"), -1);
        } else if (out->instances == 1) {
            copy_str_prop(svc, CFSTR("Role"), out->role, sizeof(out->role));
            copy_str_prop(svc, CFSTR("Diag"), out->diag, sizeof(out->diag));
            copy_str_prop(svc, CFSTR("Device"), out->device, sizeof(out->device));
            copy_str_prop(svc, CFSTR("CFBundleVersion"), out->bundleVersion,
                          sizeof(out->bundleVersion));
            out->fanCount    = copy_int_prop(svc, CFSTR("FanCount"), -1);
            out->sensorCount = copy_int_prop(svc, CFSTR("SensorCount"), -1);
            out->powerCount  = copy_int_prop(svc, CFSTR("PowerCount"), -1);
            out->systemId    = copy_int_prop(svc, CFSTR("SystemId"), -1);
        }
        IOObjectRelease(svc);
    }
    IOObjectRelease(it);
    return out->instances;
}

static io_service_t find_service(int wantReady, int *instances)
{
    CFMutableDictionaryRef m = IOServiceMatching("AlienFan");
    io_iterator_t it = IO_OBJECT_NULL;
    io_service_t found = IO_OBJECT_NULL;

    *instances = 0;
    if (!m || IOServiceGetMatchingServices(kIOMainPortDefault, m, &it) != KERN_SUCCESS)
        return IO_OBJECT_NULL;

    io_service_t svc;
    while ((svc = IOIteratorNext(it)) != IO_OBJECT_NULL) {
        (*instances)++;
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

AFClient *af_open(void)
{
    gErr[0] = 0;
    int n = 0;
    io_service_t svc = find_service(1, &n);
    if (!svc) svc = find_service(0, &n);

    if (!svc) {
        if (n == 0)
            seterr("找不到 AlienFan 服务：kext 未加载，或 personality 未匹配");
        else
            seterr("有 %d 个 AlienFan 实例，但都打不开", n);
        return NULL;
    }

    AFClient *c = (AFClient *)calloc(1, sizeof(AFClient));
    if (!c) {
        IOObjectRelease(svc);
        seterr("内存不足");
        return NULL;
    }

    kern_return_t kr = IOServiceOpen(svc, mach_task_self(), 0, &c->conn);
    if (kr != KERN_SUCCESS) {
        IOObjectRelease(svc);
        free(c);
        seterr("IOServiceOpen 失败（0x%x）", kr);
        return NULL;
    }
    c->svc = svc;
    return c;
}

void af_close(AFClient *c)
{
    if (!c) return;
    if (c->conn) IOServiceClose(c->conn);
    if (c->svc)  IOObjectRelease(c->svc);
    free(c);
}

static kern_return_t call(AFClient *c, uint32_t sel, const uint64_t *in, uint32_t inCnt,
                          uint64_t *out, uint32_t outCnt)
{
    if (!c || !c->conn) return KERN_INVALID_ARGUMENT;
    return IOConnectCallScalarMethod(c->conn, sel, in, inCnt, out, &outCnt);
}

int af_info(AFClient *c, AFInfo *o)
{
    uint64_t out[8] = {0};
    if (call(c, kAFInfo, NULL, 0, out, 8) != KERN_SUCCESS) return -1;
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

int af_fans(AFClient *c, AFFan *out, int max)
{
    AFInfo i;
    if (af_info(c, &i) != 0) return 0;
    int n = i.fans < max ? i.fans : max;
    for (int f = 0; f < n; f++) {
        uint64_t in[1] = { (uint64_t)f };
        uint64_t o[4] = {0};
        if (call(c, kAFGetFan, in, 1, o, 4) == KERN_SUCCESS) {
            out[f].rpm     = (int)(int32_t)o[0];
            out[f].percent = (int)(int32_t)o[1];
            out[f].boost   = (int)(int32_t)o[2];
            out[f].maxRpm  = (int)(int32_t)o[3];
        } else {
            memset(&out[f], 0, sizeof(out[f]));
            out[f].rpm = out[f].maxRpm = -1;
        }
    }
    return n;
}

int af_fan_ids(AFClient *c, int *out, int max)
{
    AFInfo i;
    if (af_info(c, &i) != 0) return 0;
    int n = i.fans < max ? i.fans : max;
    for (int f = 0; f < n; f++) {
        uint64_t in[1] = { (uint64_t)f }, o[1] = {0};
        out[f] = (call(c, kAFFanId, in, 1, o, 1) == KERN_SUCCESS) ? (int)(int32_t)o[0] : 0;
    }
    return n;
}

int af_temps(AFClient *c, int *out, int max)
{
    AFInfo i;
    if (af_info(c, &i) != 0) return 0;
    int n = i.sensors < max ? i.sensors : max;
    for (int s = 0; s < n; s++) {
        uint64_t in[1] = { (uint64_t)s }, o[1] = {0};
        out[s] = (call(c, kAFSensor, in, 1, o, 1) == KERN_SUCCESS) ? (int)(int32_t)o[0] : -1;
    }
    return n;
}

int af_powers(AFClient *c, int *out, int max)
{
    AFInfo i;
    if (af_info(c, &i) != 0) return 0;
    int n = i.powers < max ? i.powers : max;
    for (int p = 0; p < n; p++) {
        uint64_t in[1] = { (uint64_t)p }, o[1] = {0};
        out[p] = (call(c, kAFPowerList, in, 1, o, 1) == KERN_SUCCESS) ? (int)(uint8_t)o[0] : 0;
    }
    return n;
}

int af_get_power(AFClient *c, int *raw, int *index)
{
    uint64_t o[2] = {0};
    if (call(c, kAFGetPower, NULL, 0, o, 2) != KERN_SUCCESS) return -1;
    if (raw)   *raw   = (int)(uint8_t)o[0];
    if (index) *index = (int)(int32_t)o[1];
    return 0;
}

int af_set_power_raw(AFClient *c, int raw)
{
    uint64_t in[1] = { (uint64_t)raw };
    return call(c, kAFSetPower, in, 1, NULL, 0) == KERN_SUCCESS ? 0 : -1;
}

int af_get_gmode(AFClient *c, int *on)
{
    uint64_t o[1] = {0};
    if (call(c, kAFGetGMode, NULL, 0, o, 1) != KERN_SUCCESS) return -1;
    if (on) *on = (int)(int32_t)o[0];
    return 0;
}

int af_set_gmode(AFClient *c, int on)
{
    uint64_t in[1] = { (uint64_t)(on ? 1 : 0) };
    return call(c, kAFSetGMode, in, 1, NULL, 0) == KERN_SUCCESS ? 0 : -1;
}

int af_set_boost(AFClient *c, int fanIndex, int percent)
{
    if (percent < 0 || percent > 100) return -1;
    uint64_t in[2] = { (uint64_t)fanIndex, (uint64_t)percent };
    return call(c, kAFSetBoost, in, 2, NULL, 0) == KERN_SUCCESS ? 0 : -1;
}

int af_set_all_boost(AFClient *c, int percent, int fanCount)
{
    int rc = 0;
    for (int i = 0; i < fanCount; i++)
        if (af_set_boost(c, i, percent) != 0) rc = -1;
    return rc;
}

int af_raw(AFClient *c, int com, int sub, int a1, int a2)
{
    uint64_t in[4] = { (uint64_t)com, (uint64_t)sub, (uint64_t)a1, (uint64_t)a2 };
    uint64_t o[1] = {0};
    if (call(c, kAFRaw, in, 4, o, 1) != KERN_SUCCESS) return -1;
    return (int)(int32_t)o[0];
}
