#pragma once

#include <stdint.h>

void psvsProbeInit(volatile uint32_t *baseclk);
void psvsProbeShutdown(void);
void psvsProbeClockEvent(int requested_mhz, int effective_mhz);
void psvsProbeStatus(const char *message, int value);
