#pragma once

#include <stdint.h>

void psvsProbeInit(volatile uint32_t *baseclk);
void psvsProbeShutdown(void);
uint32_t psvsProbeMeasureCpuMhz(void);
void psvsProbeClockEvent(int requested_mhz, int effective_mhz);
void psvsProbeExperimentSample(const char *stage, uint32_t raw_mul, uint32_t raw_div,
                               uint32_t measured_mhz, uint32_t predicted_mhz);
void psvsProbeStatus(const char *message, int value);
