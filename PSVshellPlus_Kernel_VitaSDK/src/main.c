#include <vitasdkkern.h>
#include <taihen.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "abi.h"
#include "probe.h"

#define PSVS_STATE_SLOTS 32
#define HOOK_COUNT 5

int module_get_offset(SceUID pid, SceUID modid, int segidx, size_t offset, uintptr_t *addr);
int module_get_export_func(SceUID pid, const char *modname, uint32_t libnid,
                           uint32_t funcnid, uintptr_t *func);

typedef struct PsvsPidState {
    SceUID pid;
    uint32_t lock_mask;
    int custom_cpu_mhz;
} PsvsPidState;

static tai_hook_ref_t s_hook_ref[HOOK_COUNT];
static SceUID s_hook_id[HOOK_COUNT];
static SceUID s_inject_id = -1;

static volatile int s_state_guard = 0;
static PsvsPidState s_states[PSVS_STATE_SLOTS];
static volatile SceUID s_clocking_pid = -1;
static volatile int s_fps_target = -1;
static volatile int s_fps = 0;
static SceInt64 s_last_frame_us = 0;

static volatile uint32_t **s_baseclk_pp = NULL;
static int (*s_pervasive_arm_clock_select)(int mul, int ndiv) = NULL;
static int (*s_gpu_get_internal)(int *corefreq, int *mpfreq) = NULL;
static int (*s_gpu_set_internal)(int corefreq, int mpfreq) = NULL;
static int (*s_syscon_get_battery_current)(int *current) = NULL;

static void state_lock(void)
{
    while (__sync_lock_test_and_set(&s_state_guard, 1)) {
    }
}

static void state_unlock(void)
{
    __sync_lock_release(&s_state_guard);
}

static int state_find_slot_locked(SceUID pid, int create)
{
    int free_slot = -1;

    for (int i = 0; i < PSVS_STATE_SLOTS; ++i) {
        if (s_states[i].pid == pid)
            return i;
        if (free_slot < 0 && s_states[i].pid == -1)
            free_slot = i;
    }

    if (create && free_slot >= 0) {
        s_states[free_slot].pid = pid;
        s_states[free_slot].lock_mask = 0;
        s_states[free_slot].custom_cpu_mhz = PSVS_CUSTOM_CPU_NONE;
        return free_slot;
    }

    return -1;
}

static void state_set_custom_cpu(SceUID pid, int mhz)
{
    if (pid < 0)
        return;

    state_lock();
    int slot = state_find_slot_locked(pid, 1);
    if (slot >= 0)
        s_states[slot].custom_cpu_mhz = mhz;
    state_unlock();
}

static int state_get_custom_cpu(SceUID pid)
{
    int mhz = PSVS_CUSTOM_CPU_NONE;
    if (pid < 0)
        return mhz;

    state_lock();
    int slot = state_find_slot_locked(pid, 0);
    if (slot >= 0)
        mhz = s_states[slot].custom_cpu_mhz;
    state_unlock();
    return mhz;
}

SceInt32 psvsClockFrequencyLockProc(SceUID pid, PsvsLockDevice type)
{
    if (pid < 0)
        return SCE_KERNEL_ERROR_INVALID_ARGUMENT;

    state_lock();
    int slot = state_find_slot_locked(pid, 1);
    if (slot >= 0)
        s_states[slot].lock_mask |= (uint32_t)type;
    state_unlock();

    return slot >= 0 ? 0 : SCE_KERNEL_ERROR_NO_MEMORY;
}

SceInt32 psvsClockFrequencyUnlockProc(SceUID pid, PsvsLockDevice type)
{
    if (pid < 0)
        return SCE_KERNEL_ERROR_INVALID_ARGUMENT;

    state_lock();
    int slot = state_find_slot_locked(pid, 0);
    if (slot >= 0)
        s_states[slot].lock_mask &= ~((uint32_t)type);
    state_unlock();

    return 0;
}

SceBool psvsClockFrequencyIsLockedProc(SceUID pid, PsvsLockDevice type)
{
    if (pid < 0)
        return SCE_FALSE;

    uint32_t mask = 0;
    state_lock();
    int slot = state_find_slot_locked(pid, 0);
    if (slot >= 0)
        mask = s_states[slot].lock_mask;
    state_unlock();

    return (mask & (uint32_t)type) ? SCE_TRUE : SCE_FALSE;
}

static int display_set_framebuf_patched(int head, int index, const void *param, int sync)
{
    if (head == 0 && param && index == s_fps_target) {
        SceInt64 now = ksceKernelGetSystemTimeWide();
        if (s_last_frame_us > 0) {
            SceInt64 delta = now - s_last_frame_us;
            if (delta > 0)
                s_fps = (int)((1000000 + delta / 2) / delta);
        }
        s_last_frame_us = now;
    }

    return TAI_CONTINUE(int, s_hook_ref[0], head, index, param, sync);
}

static int arm_clock_set_patched(int clock)
{
    SceUID pid = s_clocking_pid;
    int forced = clock > PSVS_CLOCK_MAGIC;

    if (!psvsClockFrequencyIsLockedProc(pid, PSVS_LOCK_DEVICE_CPU) || forced) {
        if (forced)
            clock -= PSVS_CLOCK_MAGIC;

        if (clock == 500) {
            int ret = TAI_CONTINUE(int, s_hook_ref[1], 444);
            if (ret < 0)
                return ret;
            if (!s_pervasive_arm_clock_select)
                return SCE_KERNEL_ERROR_UNSUP;

            ret = s_pervasive_arm_clock_select(15, 16);
            if (ret >= 0) {
                state_set_custom_cpu(pid, 500);
                psvsProbeClockEvent(500, 500);
            }
            return ret;
        }

        state_set_custom_cpu(pid, PSVS_CUSTOM_CPU_NONE);
        int ret = TAI_CONTINUE(int, s_hook_ref[1], clock);
        if (ret >= 0)
            psvsProbeClockEvent(clock, clock);
        return ret;
    }

    return 0;
}

static int bus_clock_set_patched(int clock)
{
    int forced = clock > PSVS_CLOCK_MAGIC;
    if (!psvsClockFrequencyIsLockedProc(s_clocking_pid, PSVS_LOCK_DEVICE_BUS) || forced) {
        if (forced)
            clock -= PSVS_CLOCK_MAGIC;
        return TAI_CONTINUE(int, s_hook_ref[2], clock);
    }
    return 0;
}

static int gpu_clock_set_patched(int corefreq, int mpfreq)
{
    int forced = corefreq > PSVS_CLOCK_MAGIC;
    if (!psvsClockFrequencyIsLockedProc(s_clocking_pid, PSVS_LOCK_DEVICE_GPU_ES4) || forced) {
        if (forced) {
            corefreq -= PSVS_CLOCK_MAGIC;
            mpfreq -= PSVS_CLOCK_MAGIC;
        }
        return TAI_CONTINUE(int, s_hook_ref[3], corefreq, mpfreq);
    }
    return 0;
}

static int gpu_xbar_clock_set_patched(int clock)
{
    int forced = clock > PSVS_CLOCK_MAGIC;
    if (!psvsClockFrequencyIsLockedProc(s_clocking_pid, PSVS_LOCK_DEVICE_GPU_XBAR) || forced) {
        if (forced)
            clock -= PSVS_CLOCK_MAGIC;
        return TAI_CONTINUE(int, s_hook_ref[4], clock);
    }
    return 0;
}

SceInt32 psvsSetArmClockFrequency(SceInt32 clock)
{
    uint32_t state;
    ENTER_SYSCALL(state);
    int ret = kscePowerSetArmClockFrequency(clock + PSVS_CLOCK_MAGIC);
    EXIT_SYSCALL(state);
    return ret;
}

SceInt32 psvsSetGpuClockFrequency(SceInt32 clock)
{
    uint32_t state;
    ENTER_SYSCALL(state);
    int ret = s_gpu_set_internal
        ? s_gpu_set_internal(clock + PSVS_CLOCK_MAGIC, clock + PSVS_CLOCK_MAGIC)
        : SCE_KERNEL_ERROR_UNSUP;
    EXIT_SYSCALL(state);
    return ret;
}

SceInt32 psvsSetGpuXbarClockFrequency(SceInt32 clock)
{
    uint32_t state;
    ENTER_SYSCALL(state);
    int ret = kscePowerSetGpuXbarClockFrequency(clock + PSVS_CLOCK_MAGIC);
    EXIT_SYSCALL(state);
    return ret;
}

SceInt32 psvsSetBusClockFrequency(SceInt32 clock)
{
    uint32_t state;
    ENTER_SYSCALL(state);
    int ret = kscePowerSetBusClockFrequency(clock + PSVS_CLOCK_MAGIC);
    EXIT_SYSCALL(state);
    return ret;
}

SceInt32 psvsGetClockFrequency(PSVSClockFrequency *clocks)
{
    if (!clocks)
        return SCE_KERNEL_ERROR_INVALID_ARGUMENT;

    uint32_t state;
    ENTER_SYSCALL(state);

    PSVSClockFrequency out;
    int gpu_mp = 0;
    out.cpu = kscePowerGetArmClockFrequency();
    int custom = state_get_custom_cpu(s_clocking_pid);
    if (custom > 0)
        out.cpu = custom;
    out.gpu = 0;
    if (s_gpu_get_internal)
        s_gpu_get_internal(&out.gpu, &gpu_mp);
    out.xbar = kscePowerGetGpuXbarClockFrequency();
    out.bus = kscePowerGetBusClockFrequency();

    int ret = ksceKernelCopyToUser(clocks, &out, sizeof(out));
    EXIT_SYSCALL(state);
    return ret;
}

SceInt32 psvsGetFps(void)
{
    return s_fps;
}

SceVoid psvsSetFpsCounterTarget(SceInt32 target)
{
    s_fps_target = target;
}

SceVoid psvsSetClockingPid(SceUID pid)
{
    s_clocking_pid = pid;
}

SceInt32 psvsGetMem(SceUInt32 casShift, PSVSMem *mem)
{
    (void)casShift;
    if (!mem)
        return SCE_KERNEL_ERROR_INVALID_ARGUMENT;

    uint32_t state;
    ENTER_SYSCALL(state);
    PSVSMem out;
    memset(&out, 0, sizeof(out));
    int ret = ksceKernelCopyToUser(mem, &out, sizeof(out));
    EXIT_SYSCALL(state);
    return ret;
}

SceInt32 psvsSetRecommendedCasShift(char *name, SceInt32 namelen)
{
    (void)name;
    (void)namelen;
    return 0;
}

SceInt32 psvsGetVeneziaInfo(PSVSVenezia *data)
{
    if (!data)
        return SCE_KERNEL_ERROR_INVALID_ARGUMENT;

    uint32_t state;
    ENTER_SYSCALL(state);
    PSVSVenezia out;
    memset(&out, 0, sizeof(out));
    int ret = ksceKernelCopyToUser(data, &out, sizeof(out));
    EXIT_SYSCALL(state);
    return ret < 0 ? ret : SCE_KERNEL_ERROR_UNSUP;
}

SceInt32 psvsGetBatteryInfo(PSVSBattery *data)
{
    if (!data)
        return SCE_KERNEL_ERROR_INVALID_ARGUMENT;

    uint32_t state;
    ENTER_SYSCALL(state);
    PSVSBattery out;
    out.current = 0;
    int call_ret = s_syscon_get_battery_current
        ? s_syscon_get_battery_current(&out.current)
        : SCE_KERNEL_ERROR_UNSUP;
    out.current = -out.current;
    int copy_ret = ksceKernelCopyToUser(data, &out, sizeof(out));
    EXIT_SYSCALL(state);
    return copy_ret < 0 ? copy_ret : call_ret;
}

void _start(void) __attribute__((weak, alias("module_start")));
int module_start(SceSize argc, const void *args)
{
    (void)argc;
    (void)args;

    for (int i = 0; i < HOOK_COUNT; ++i)
        s_hook_id[i] = -1;
    for (int i = 0; i < PSVS_STATE_SLOTS; ++i)
        s_states[i].pid = -1;

    tai_module_info_t info;
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    if (taiGetModuleInfoForKernel(KERNEL_PID, "SceLowio", &info) >= 0)
        module_get_offset(KERNEL_PID, info.modid, 1, 0xA0, (uintptr_t *)&s_baseclk_pp);

    module_get_export_func(KERNEL_PID, "SceLowio", 0xE692C727, 0xE9D95643,
                           (uintptr_t *)&s_pervasive_arm_clock_select);
    module_get_export_func(KERNEL_PID, "ScePower", 0x1590166F, 0x475BCC82,
                           (uintptr_t *)&s_gpu_get_internal);
    module_get_export_func(KERNEL_PID, "ScePower", 0x1590166F, 0x264C24FC,
                           (uintptr_t *)&s_gpu_set_internal);
    module_get_export_func(KERNEL_PID, "SceSyscon", 0x60A35F64, 0x0826BA07,
                           (uintptr_t *)&s_syscon_get_battery_current);

    volatile uint32_t *baseclk = s_baseclk_pp ? *s_baseclk_pp : NULL;
    psvsProbeInit(baseclk);
    psvsProbeStatus("pervasive_arm_clock_select", s_pervasive_arm_clock_select != NULL);
    psvsProbeStatus("gpu_get_internal", s_gpu_get_internal != NULL);
    psvsProbeStatus("gpu_set_internal", s_gpu_set_internal != NULL);

    s_hook_id[0] = taiHookFunctionExportForKernel(
        KERNEL_PID, &s_hook_ref[0], "SceDisplay", 0x9FED47AC, 0x16466675,
        display_set_framebuf_patched);
    s_hook_id[1] = taiHookFunctionExportForKernel(
        KERNEL_PID, &s_hook_ref[1], "ScePower", 0x1590166F, 0x74DB5AE5,
        arm_clock_set_patched);
    s_hook_id[2] = taiHookFunctionExportForKernel(
        KERNEL_PID, &s_hook_ref[2], "ScePower", 0x1590166F, 0xB8D7B3FB,
        bus_clock_set_patched);
    s_hook_id[3] = taiHookFunctionExportForKernel(
        KERNEL_PID, &s_hook_ref[3], "ScePower", 0x1590166F, 0x264C24FC,
        gpu_clock_set_patched);
    s_hook_id[4] = taiHookFunctionExportForKernel(
        KERNEL_PID, &s_hook_ref[4], "ScePower", 0x1590166F, 0xA7739DBE,
        gpu_xbar_clock_set_patched);

    if (s_pervasive_arm_clock_select) {
        const uint8_t nop[] = {0x00, 0xBF};
        s_inject_id = taiInjectAbsForKernel(
            KERNEL_PID,
            (void *)((uintptr_t)s_pervasive_arm_clock_select + 0x1D),
            nop, sizeof(nop));
    }

    psvsProbeStatus("inject_id", s_inject_id);
    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args)
{
    (void)argc;
    (void)args;

    for (int i = 0; i < HOOK_COUNT; ++i) {
        if (s_hook_id[i] >= 0)
            taiHookReleaseForKernel(s_hook_id[i], s_hook_ref[i]);
    }

    if (s_inject_id >= 0)
        taiInjectReleaseForKernel(s_inject_id);

    psvsProbeShutdown();
    return SCE_KERNEL_STOP_SUCCESS;
}
