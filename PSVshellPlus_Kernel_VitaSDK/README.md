# PSVshellPlus Kernel - VitaSDK experimental backend

This directory contains an experimental VitaSDK build of the PSVshellPlus kernel module.
It is intentionally kept separate from the original Sony/VDSuite project so the upstream
v1.4 source remains untouched and easy to compare/revert.

## Goals of this milestone

- Preserve the original `PSVshellPlus_KernelForUser` syscall ABI used by the v1.4 shell.
- Keep the public `PSVSClockFrequency` ABI at exactly four 32-bit fields (16 bytes).
- Reproduce the existing 500 MHz path (`444 MHz` first, then Pervasive ARM selector `15:16`).
- Log raw ARM BaseClk registers and measure real CPU frequency with the Cortex-A9 PMU.
- Capture one differential sample at 333, 444 and 500 MHz before attempting higher clocks.

This build **does not claim 600/700/800 MHz support**. Public Vita code consistently tops
out at the `0xF:0` ARM BaseClk setting. The next step is to identify the parent clock/PLL
feeding that selector from real hardware measurements.

## Build

```sh
export VITASDK=/usr/local/vitasdk
cmake -S PSVshellPlus_Kernel_VitaSDK -B build-vitasdk
cmake --build build-vitasdk
```

Output:

```text
build-vitasdk/PSVshellPlus_Kernel.skprx
```

GitHub Actions builds the same target with the official `vitasdk/vitasdk:latest` image.

## Hardware test

Use the normal PSVshellPlus v1.4 shell/RCO and replace only the kernel module with the
experimental `.skprx`.

Start conservatively and select these CPU clocks in this order:

1. 333 MHz
2. 444 MHz
3. 500 MHz

After the test, copy:

```text
ur0:data/PSVshell/arm_oc_probe.log
```

The log records:

- requested/effective CPU MHz;
- PMU-measured MHz (cycle counter versus system microsecond timer);
- ARM BaseClk raw multiplier/divider registers;
- the first 16 words of the BaseClk block for each of 333/444/500 MHz.

## Current compatibility notes

Clock control, lock state, FPS tracking and battery current are implemented. The first
VitaSDK milestone returns zeroed memory statistics and reports Venezia load as unsupported;
these non-overclock telemetry paths will be ported after the clock backend is hardware
validated.

The PID lock state is maintained in a kernel-side table instead of Sony's private KPLS API,
which is not exposed by public VitaSDK headers.
