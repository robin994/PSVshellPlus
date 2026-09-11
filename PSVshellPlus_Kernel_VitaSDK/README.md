# PSVshellPlus Kernel - VitaSDK experimental backend

This directory contains an experimental VitaSDK build of the PSVshellPlus kernel module.
It is intentionally kept separate from the original Sony/VDSuite project so the upstream
v1.4 source remains untouched and easy to compare/revert.

## Important compatibility status

**Do not currently use this VitaSDK kernel as a drop-in replacement for the official v1.4
`PSVshellPlus_Kernel.skprx`.** Hardware testing showed that the experimental kernel starts
and performs clock measurements, but the official v1.4 shell does not appear in the Quick
Menu when paired with it. Even after matching the Sony stub library/function NIDs, the
binary compatibility issue remains.

For a working Quick Menu, keep the official v1.4 trio installed exactly as upstream:

```text
*KERNEL
ur0:tai/PSVshellPlus_Kernel.skprx

*main
ur0:tai/PSVshellPlus_Shell.suprx
```

and keep `psvshell_plugin.rco` at `ur0:data/PSVshell/psvshell_plugin.rco`.

Further ARM overclock research should use a separate research kernel plugin so the official
PSVshellPlus kernel/shell ABI remains untouched.

## Research goals

- Keep the public `PSVSClockFrequency` ABI at exactly four 32-bit fields (16 bytes).
- Reproduce the existing 500 MHz path (`444 MHz` first, then Pervasive ARM selector `15:16`).
- Log raw ARM BaseClk registers and measure real CPU frequency with the Cortex-A9 PMU.
- Experiment with an alternate BaseClk source conservatively instead of overflowing the
  known 4-bit multiplier beyond `0xF`.

Public Vita code consistently tops out at the `0xF:0` ARM BaseClk setting for the normal
source. The source-16 experiment is a reverse-engineering hypothesis based on the fact that
other devices inside the same Pervasive BaseClk block use bit 16 as a clock-source selector.
It is not proof that the ARM clock uses the same encoding.

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

## Hardware observations

The PMU probe has already validated the known clock path on real hardware: the raw ARM
BaseClk register was `0x7:0` near 333 MHz and `0xE:0` near 444 MHz, with PMU measurements
close to the requested clocks. The alternate-source experiment has not yet been executed
successfully because the shell compatibility problem prevented selecting the 500 MHz trigger.

The probe log is written to:

```text
ur0:data/PSVshell/arm_oc_probe.log
```

Future experiments should be moved to the standalone OC-probe module rather than replacing
the official PSVshellPlus kernel.
