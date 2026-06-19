#include "GBMacBookMotion.h"
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <pthread.h>
#include <string.h>

#define ACCEL_SCALE 65536.0
#define IMU_REPORT_LEN 22
#define IMU_DATA_OFF 6

// Both kIOMainPortDefault and kIOMasterPortDefault are 0
#define MAIN_PORT 0

static IOHIDDeviceRef s_device;
static CFRunLoopRef s_run_loop;
static pthread_t s_thread;
static volatile bool s_running;

static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile bool s_has_data;
static double s_x, s_y, s_z;

static uint8_t s_report_buffer[4096];

static void report_callback(void *context, IOReturn result, void *sender,
                             IOHIDReportType type, uint32_t reportID,
                             uint8_t *report, CFIndex reportLength)
{
    if (reportLength != IMU_REPORT_LEN) return;

    int32_t raw_x, raw_y, raw_z;
    memcpy(&raw_x, report + IMU_DATA_OFF, 4);
    memcpy(&raw_y, report + IMU_DATA_OFF + 4, 4);
    memcpy(&raw_z, report + IMU_DATA_OFF + 8, 4);

    pthread_mutex_lock(&s_mutex);
    s_x = raw_x / ACCEL_SCALE;
    s_y = raw_y / ACCEL_SCALE;
    s_z = raw_z / ACCEL_SCALE;
    s_has_data = true;
    pthread_mutex_unlock(&s_mutex);
}

static void wake_spu_drivers(void)
{
    CFMutableDictionaryRef matching = IOServiceMatching("AppleSPUHIDDriver");
    if (!matching) return;

    io_iterator_t iter = 0;
    kern_return_t kr = IOServiceGetMatchingServices(MAIN_PORT, matching, &iter);
    if (kr != KERN_SUCCESS) return;

    io_service_t svc;
    while ((svc = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
        int32_t one = 1, interval = 1000;
        CFNumberRef v_one = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &one);
        CFNumberRef v_interval = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &interval);

        IORegistryEntrySetCFProperty(svc, CFSTR("SensorPropertyReportingState"), v_one);
        IORegistryEntrySetCFProperty(svc, CFSTR("SensorPropertyPowerState"), v_one);
        IORegistryEntrySetCFProperty(svc, CFSTR("ReportInterval"), v_interval);

        CFRelease(v_one);
        CFRelease(v_interval);
        IOObjectRelease(svc);
    }
    IOObjectRelease(iter);
}

static IOHIDDeviceRef find_accelerometer_device(void)
{
    CFMutableDictionaryRef matching = IOServiceMatching("AppleSPUHIDDevice");
    if (!matching) return NULL;

    io_iterator_t iter = 0;
    kern_return_t kr = IOServiceGetMatchingServices(MAIN_PORT, matching, &iter);
    if (kr != KERN_SUCCESS) return NULL;

    IOHIDDeviceRef found = NULL;
    io_service_t service;
    while ((service = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
        CFNumberRef usage_page_ref = IORegistryEntryCreateCFProperty(service, CFSTR("PrimaryUsagePage"), kCFAllocatorDefault, 0);
        CFNumberRef usage_ref = IORegistryEntryCreateCFProperty(service, CFSTR("PrimaryUsage"), kCFAllocatorDefault, 0);

        int32_t usage_page = 0, usage = 0;
        if (usage_page_ref) {
            CFNumberGetValue(usage_page_ref, kCFNumberSInt32Type, &usage_page);
            CFRelease(usage_page_ref);
        }
        if (usage_ref) {
            CFNumberGetValue(usage_ref, kCFNumberSInt32Type, &usage);
            CFRelease(usage_ref);
        }

        if (usage_page == 0xFF00 && usage == 3) {
            found = IOHIDDeviceCreate(kCFAllocatorDefault, service);
            IOObjectRelease(service);
            break;
        }
        IOObjectRelease(service);
    }
    IOObjectRelease(iter);
    return found;
}

static void *run_loop_thread(void *arg)
{
    s_run_loop = CFRunLoopGetCurrent();

    IOHIDDeviceRegisterInputReportCallback(s_device, s_report_buffer, sizeof(s_report_buffer),
                                           report_callback, NULL);
    IOHIDDeviceScheduleWithRunLoop(s_device, s_run_loop, kCFRunLoopDefaultMode);

    while (s_running) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.5, false);
    }

    return NULL;
}

bool GB_macbook_motion_start(void)
{
    if (s_running) return true;

    wake_spu_drivers();

    s_device = find_accelerometer_device();
    if (!s_device) return false;

    IOReturn ret = IOHIDDeviceOpen(s_device, kIOHIDOptionsTypeNone);
    if (ret != kIOReturnSuccess) {
        CFRelease(s_device);
        s_device = NULL;
        return false;
    }

    s_running = true;
    s_has_data = false;

    pthread_create(&s_thread, NULL, run_loop_thread, NULL);

    return true;
}

void GB_macbook_motion_stop(void)
{
    if (!s_running) return;
    s_running = false;

    if (s_run_loop) {
        CFRunLoopStop(s_run_loop);
    }

    pthread_join(s_thread, NULL);
    s_run_loop = NULL;

    if (s_device) {
        IOHIDDeviceClose(s_device, kIOHIDOptionsTypeNone);
        CFRelease(s_device);
        s_device = NULL;
    }

    s_has_data = false;
}

bool GB_macbook_motion_poll(double *x, double *y, double *z)
{
    pthread_mutex_lock(&s_mutex);
    bool has = s_has_data;
    if (has) {
        *x = s_x;
        *y = s_y;
        *z = s_z;
    }
    pthread_mutex_unlock(&s_mutex);
    return has;
}
