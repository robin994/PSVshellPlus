#include <vitasdkkern.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>

#include "probe.h"

#define PROBE_LOG_DIR  "ur0:data/PSVshell"
#define PROBE_LOG_PATH "ur0:data/PSVshell/arm_oc_probe.log"
#define BASECLK_DUMP_WORDS 16

static SceUID s_log_fd = -1;
static volatile uint32_t *s_baseclk = NULL;
static uint32_t s_sample_mask = 0;

static void probe_log(const char *fmt, ...)
{
    if (s_log_fd < 0)
        return;

    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (len <= 0)
        return;
    if (len >= (int)sizeof(buf))
        len = sizeof(buf) - 1;

    ksceIoWrite(s_log_fd, buf, len);
}

static inline void pmu_enable_cycle_counter(void)
{
    uint32_t pmcr;
    uint32_t enable = 1u << 31;

    __asm__ volatile("mrc p15, 0, %0, c9, c12, 0" : "=r"(pmcr));
    pmcr |= (1u << 0);
    pmcr |= (1u << 2);
    pmcr &= ~(1u << 3);
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 0" :: "r"(pmcr) : "memory");
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 1" :: "r"(enable) : "memory");
    __asm__ volatile("isb" ::: "memory");
}

static inline uint32_t pmu_read_cycle_counter(void)
{
    uint32_t value;
    __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(value));
    return value;
}

static uint32_t measure_cpu_mhz(void)
{
    volatile uint32_t sink = 0x12345678u;
    const SceInt64 target_us = 10000;

    pmu_enable_cycle_counter();
    SceInt64 t0 = ksceKernelGetSystemTimeWide();
    uint32_t c0 = pmu_read_cycle_counter();
    SceInt64 now = t0;

    do {
        for (int i = 0; i < 256; ++i)
            sink = sink * 1664525u + 1013904223u;
        now = ksceKernelGetSystemTimeWide();
    } while ((now - t0) < target_us);

    uint32_t c1 = pmu_read_cycle_counter();
    SceInt64 elapsed = now - t0;
    uint32_t cycles = c1 - c0;

    (void)sink;
    if (elapsed <= 0 || cycles == 0)
        return 0;

    return (uint32_t)((cycles + (uint32_t)(elapsed / 2)) / (uint32_t)elapsed);
}

static uint32_t sample_bit_for_clock(int mhz)
{
    switch (mhz) {
        case 333: return 1u << 0;
        case 444: return 1u << 1;
        case 500: return 1u << 2;
        default: return 0;
    }
}

static void dump_baseclk(void)
{
    if (!s_baseclk) {
        probe_log("BASECLK unavailable\n");
        return;
    }

    for (uint32_t i = 0; i < BASECLK_DUMP_WORDS; ++i)
        probe_log("BASECLK +0x%02X = 0x%08X\n", i * 4, s_baseclk[i]);
}

void psvsProbeInit(volatile uint32_t *baseclk)
{
    s_baseclk = baseclk;
    ksceIoMkdir(PROBE_LOG_DIR, 0777);
    s_log_fd = ksceIoOpen(PROBE_LOG_PATH,
                         SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND,
                         0666);

    probe_log("\n=== PSVshellPlus ARM OC probe start t=%lld ===\n",
              (long long)ksceKernelGetSystemTimeWide());
    probe_log("baseclk_va=0x%08lX\n", (unsigned long)s_baseclk);
}

void psvsProbeShutdown(void)
{
    probe_log("=== PSVshellPlus ARM OC probe stop ===\n");
    if (s_log_fd >= 0) {
        ksceIoClose(s_log_fd);
        s_log_fd = -1;
    }
}

void psvsProbeStatus(const char *message, int value)
{
    probe_log("STATUS %s=%d (0x%08X)\n", message ? message : "?", value, (uint32_t)value);
}

void psvsProbeClockEvent(int requested_mhz, int effective_mhz)
{
    uint32_t bit = sample_bit_for_clock(effective_mhz);
    uint32_t raw_mul = s_baseclk ? s_baseclk[0] : 0xFFFFFFFFu;
    uint32_t raw_div = s_baseclk ? s_baseclk[1] : 0xFFFFFFFFu;
    uint32_t measured_mhz = measure_cpu_mhz();

    probe_log("CLOCK requested=%d effective=%d measured=%u raw=%08X:%08X t=%lld\n",
              requested_mhz, effective_mhz, measured_mhz,
              raw_mul, raw_div, (long long)ksceKernelGetSystemTimeWide());

    if (!bit || (s_sample_mask & bit))
        return;

    s_sample_mask |= bit;
    probe_log("--- BaseClk sample %d MHz ---\n", effective_mhz);
    dump_baseclk();
    probe_log("--- end sample %d MHz ---\n", effective_mhz);
}
