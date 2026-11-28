# 0006: Version 1 battery-life scope

Date: 2026-11-24

Status: accepted

## Context

Battery operation and power-saving controls exist, but current consumption and
battery life have not been measured. Version 1 has no battery-voltage ADC or
USB-supply-voltage measurement.

## Decision

Version 1 promises no battery life. Autonomy is conditional and undocumented.
Do not specify runtime, charge percentage or remaining time. A battery ADC and
its driver may be considered in version 2; they are not a version 1 requirement.

## Evidence

Source: the owner's decision of 2026-11-24 and the hardware/power sections of
[CLAUDE.md](../../CLAUDE.md), recorded by commits `953581e` and `00da628`.

The assembled battery is nominally 4,000 mAh, one-cell 3.7 V Li-Ion with a
protection board. That capacity is a hardware specification, not a measured
usable capacity or a runtime result. There are no measured current or endurance
numbers from which to derive a battery-life promise.

Power saving retains application state in light sleep, with a 300-second minimum
interval for scheduled polls and a two-minute user-access window. Explicit refresh
requests can run sooner, subject to queueing and server cooldowns. Auto samples the
existing USB Serial/JTAG host connection every five seconds; a charger does not constitute a
USB host. This signal does not identify the actual power source or charge state.
An attached computer inhibits automatic light sleep, so observing the schedule
over USB does not establish battery current. Deep sleep is deferred to version 2.

## Consequences

Documentation and interfaces must not imply quantified autonomy or battery
telemetry. Existing light-sleep/radio scheduling remains available. Version 2
ADC work requires separate hardware, calibration and driver decisions; voltage
readings alone do not establish runtime.

## Reopen when

Version 2 hardware scope is approved, or representative current/capacity and
runtime measurements support a separately approved battery-life specification.
