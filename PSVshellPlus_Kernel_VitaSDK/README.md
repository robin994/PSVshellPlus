# PSVshellPlus Kernel - VitaSDK experimental backend

This directory contains an experimental VitaSDK build of the PSVshellPlus kernel module.
It is intentionally kept separate from the original Sony/VDSuite project so the upstream
v1.4 source remains untouched and easy to compare/revert.

## Goals of this milestone

- Preserve the original `PSVshellPlus_KernelForUser` syscall ABI used by the v1.4 shell.
- Keep the public `PSVSClockFrequency` ABI at exactly four 32-bit fields (16 bytes).
- Reproduce the existing 500 MHz path (`444 MHz` first, then Pervasive ARM selector `15:16`).
- Log raw ARM BaseClk registers and measure real CPU frequency with the Cortex-A9 PMU.
- Experiment with an alternate BaseClk source conservatively instead of overflowing the
  known 4-bit multiplier beyond `0xF`.

Public Vita code consistently tops out at the `0xF:0` ARM BaseClk setting for the normal
source. The source-16 experiment below is a reverse-engineering hypothesis based on the
fact that other devices inside the same Pervasive BaseClk block use bit 16 as a clock-source
selector. It is not proof that the ARM clock uses the same encoding.

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

## Normal hardware probe

Use the normal PSVshellPlus v1.4 shell/RCO and replace only the kernel module with the
experimental `.skprx`.

The probe log is written to:

```text
ur0:data/PSVshell/arm_oc_probe.log
```

It records requested/effective CPU MHz, PMU-measured MHz, raw ARM BaseClk values and the
first BaseClk words for known clock points.

## One-shot alternate-parent experiment

The experiment is deliberately disabled by default. It never applies a >500 MHz candidate
merely because the plugin was loaded.

To arm exactly one attempt, create an empty file while the Vita is already running:

```text
ur0:data/PSVshell/arm_oc_source16.once
```

Wait at least 15 seconds after boot and then select **500 MHz** in the normal PSVshellPlus
menu. The kernel consumes (deletes) the marker before touching the experimental source.
If the system hard-crashes, the experiment therefore will not automatically repeat after
reboot.

The sequence is adaptive:

1. Establish the known ~500 MHz `0xF:0` baseline and verify it with the PMU.
2. Try alternate-source bit 16 with multiplier 1, a deliberately low clock point.
3. Try multiplier 2 only if the first point is <=200 MHz, and require approximately linear
   scaling (within 18%).
4. Compute a multiplier in the known 1..15 range targeting about 550 MHz.
5. Apply it only if the predicted clock is 510..575 MHz.
6. Measure the result with the PMU. Keep it only when the real result is 510..600 MHz;
   otherwise restore the exact saved 500 MHz BaseClk registers.

Successful/failed stages appear as `EXPERIMENT` and `STATUS source16_*` lines in
`arm_oc_probe.log`. If the source hypothesis is correct and the final clock is accepted,
PSVshellPlus reports the PMU-measured CPU frequency through the existing v1.4 ABI until a
new CPU clock is selected.

This is still experimental overclocking. A wrong source interpretation can hard-freeze the
console before software rollback is possible. The consumed marker prevents a repeated
boot-loop, but it cannot make an invalid live clock transition harmless.

## Current compatibility notes

Clock control, lock state, FPS tracking and battery current are implemented. The first
VitaSDK milestone returns zeroed memory statistics and reports Venezia load as unsupported;
these non-overclock telemetry paths will be ported after the clock backend is hardware
validated.

The PID lock state is maintained in a kernel-side table instead of Sony's private KPLS API,
which is not exposed by public VitaSDK headers.
