#pragma once

#include <vitasdkkern.h>

#define PSVS_CLOCK_MAGIC 0x4000
#define PSVS_CUSTOM_CPU_NONE 0

typedef enum PsvsLockDevice {
    PSVS_LOCK_DEVICE_NONE = 0,
    PSVS_LOCK_DEVICE_CPU = 1,
    PSVS_LOCK_DEVICE_GPU_ES4 = 2,
    PSVS_LOCK_DEVICE_BUS = 4,
    PSVS_LOCK_DEVICE_GPU_XBAR = 8,
    PSVS_LOCK_DEVICE_ALL = PSVS_LOCK_DEVICE_CPU | PSVS_LOCK_DEVICE_GPU_ES4 |
                           PSVS_LOCK_DEVICE_BUS | PSVS_LOCK_DEVICE_GPU_XBAR
} PsvsLockDevice;

typedef struct PSVSClockFrequency {
    SceInt32 cpu;
    SceInt32 gpu;
    SceInt32 xbar;
    SceInt32 bus;
} PSVSClockFrequency;

typedef struct PSVSMem {
    SceUInt32 mainFree;
    SceUInt32 mainTotal;
    SceUInt32 cdramFree;
    SceUInt32 cdramTotal;
    SceUInt32 phycontFree;
    SceUInt32 phycontTotal;
    SceUInt32 cdialogFree;
    SceUInt32 cdialogTotal;
} PSVSMem;

typedef struct PSVSVenezia {
    SceInt32 core0;
    SceInt32 core1;
    SceInt32 core2;
    SceInt32 core3;
    SceInt32 core4;
    SceInt32 core5;
    SceInt32 core6;
    SceInt32 core7;
    SceInt32 average;
    SceInt32 peak;
} PSVSVenezia;

typedef struct PSVSBattery {
    SceInt32 current;
} PSVSBattery;
