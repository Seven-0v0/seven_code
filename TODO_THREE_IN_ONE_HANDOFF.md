# Three-in-One RoboMaster Firmware Context

## Why This Document Exists

This file gives a future conversation enough context to understand the project before discussing priorities with the user. It is not an execution plan, a continuation command, or an instruction to start a predetermined task.

The user and the next agent should decide together what matters next. The durable content here is the product vision, source-of-truth relationships, architecture, verified baseline, safety boundaries, and unresolved capability areas.

## Product Vision

The long-term goal is one layered repository containing three independently built and flashed firmware products:

1. Official RoboMaster Development Board C reference and hardware-validation firmware.
2. Team gimbal firmware for STM32F405RG.
3. Team chassis firmware for STM32F405RG.

"Three-in-one" means one repository and one common technical foundation, not one firmware image and not one MCU running all responsibilities.

The three products should share conventions and reusable implementation where the hardware and behavior genuinely overlap:

- BSP and chip-description conventions.
- Board support boundaries.
- Portable device drivers.
- FreeRTOS and shared middleware.
- Typed communication protocols.
- Host-side tests and golden fixtures.
- Build, flash, logging, and hardware-validation infrastructure.
- Physical-safety gates and evidence standards.

The intended result is not a generic framework detached from the robot. It is a common foundation that preserves the real behavior of the official board and the team's deployed gimbal and chassis firmware.

## Behavioral and Reference Sources

### Team firmware

- Gimbal: https://github.com/zy-2006-08/Gimbal_26hero_CH010_20260725
- Chassis: https://github.com/zy-2006-08/Chassisl_26_SHANGTAIJIE

These repositories are the behavioral source of truth for the actual robot. Important facts include PID values, encoder zeros, motor directions, mechanical limits, timing, CAN rates, mode transitions, deploy behavior, leg control, vision behavior, and referee HUD behavior.

The objective is to preserve those facts while improving boundaries and testability. Their monolithic `my_main.cpp` files are evidence to interpret, not files to copy wholesale.

### Official RoboMaster references

- https://github.com/RoboMaster/Development-Board-C-Examples
- https://github.com/RoboMaster/RoboRTS-Firmware
- https://github.com/RoboMaster/referee_serial_port_protocol
- https://github.com/RoboMaster/RoboRTS-Base

Official sources establish board wiring, chip behavior, peripheral conventions, referee framing, and reference implementations. They do not override team-specific vehicle behavior where the team firmware is the deployed source of truth.

`dji-sdk/RoboMaster-SDK` is a host-side EP/Tello SDK, not the embedded competition-firmware base. Its protocol-layering ideas may be useful, but it is not a source for MCU behavior.

## Architecture

The dependency direction is:

```text
app -> board -> chip -> bsp
app -> drivers
app -> middleware
```

Responsibilities:

- `apps/`: product behavior, task orchestration, and product state machines.
- `boards/`: physical pin assignments, oscillator facts, board clocks, and board-specific FreeRTOS configuration.
- `cmake/chips/`: CPU ABI, device macros, startup source, linker script, and chip-level build facts.
- `bsp/`: vendor CMSIS/HAL/LL implementation.
- `drivers/`: portable external-device and bus-independent protocol logic.
- `middleware/`: reusable control, algorithm, referee, vision, and robot-link behavior.
- `tests/firmware/`: host-native tests for pure firmware logic, separate from the ARM cross build.

Expected product shape:

```text
apps/
  rm_c_blinky/          # safe C-board hardware baseline
  standard_robot_c/     # possible official reference integration
  gimbal_f405/          # team gimbal image
  chassis_f405/         # team chassis image

boards/
  rm_dev_board_c/
  team_gimbal_f405/
  team_chassis_f405/

cmake/chips/
  stm32f407ighx.cmake
  stm32f405rg.cmake

drivers/
  can/
  motor/dji/
  motor/lk6010/
  motor/dm/
  imu/bmi088/
  imu/hipnuc/
  imu/jy61p/
  remote/dbus/

middleware/
  controller/
  algorithm/
  referee/
  vision/
  robot_link/
```

New abstractions should represent a demonstrated shared need. Compatibility layers, fallbacks, and versioning should not be added speculatively.

## Current Verified Baseline

The repository baseline through commit `2ce8212` includes:

- Selectable F103 and RoboMaster C-board build targets.
- STM32F407IGHx Cortex-M4F target with hard-float ABI.
- 1 MiB Flash, 128 KiB SRAM, and 64 KiB CCM memory description.
- Official C-board 12 MHz HSE to 168 MHz clock tree.
- FreeRTOS V11.1.0 using `GCC_ARM_CM4F`.
- TIM6 as the HAL 1 kHz timebase while FreeRTOS exclusively owns SysTick.
- PH10 LED heartbeat. The PA9 USART1 TX diagnostic path was implemented but is now removed/superseded: it was never verified over a physical serial connection (see below), and live observation of the C-board now goes exclusively through Ozone/J-Link against `g_gyro_snapshot`. A portable, repo-committed Ozone project lives at `apps/rm_c_blinky/ozone/rm_c_blinky.jdebug`.
- J-Link build, flash, and `verifybin` workflow.
- A standalone host CMake/CTest firmware test harness.
- A portable gyro-only BMI088 driver with an injected bus interface.
- C-board SPI1 integration for the BMI088 gyro.
- Fixed-point integer formatting without float `printf`.
- A generation-protected J-Link-readable gyro snapshot.

No RTT debug component is part of this baseline. RTT was explored and deliberately removed before commit `2ce8212`.

## BMI088 Gyroscope State

The gyro half of the BMI088 path is implemented and tested.

### Hardware facts

- SPI1 SCK: PB3.
- SPI1 MISO: PB4.
- SPI1 MOSI: PA7.
- Gyro chip select: PB0, active low.
- Accelerometer chip select: PA4, held high.
- SPI mode 3, software NSS, conservative prescaler.
- Gyro chip ID: `0x0F`.
- Gyro range: ±2000 dps.
- Gyro bandwidth/ODR register: `0x82`, representing 1000 Hz ODR / 116 Hz bandwidth.

### Software behavior

- The portable driver contains no STM32 HAL, board, or FreeRTOS dependency.
- Initialization performs chip-ID checks, reset, delay, configuration writes, and readback verification.
- Samples are read in one burst with the chip ID carried as a framing canary.
- Raw signed counts are converted to integer milli-degrees per second.
- The C-board app publishes a coherent `g_gyro_snapshot`. It previously also produced a USART1 telemetry path; that path is removed/superseded and no longer the documented way to observe this data (see Ozone/J-Link note below).
- The accelerometer half of the BMI088 is not accessed.

### Verification evidence

- Host firmware tests: 8/8 passing.
- F103 regression build: passing.
- C-board cross build: passing.
- Flash program and verification: passing.
- J-Link S/N: `602712225`.
- Target voltage: approximately 3.28 V.
- Gyro initialization status: success.
- Runtime read-error count: zero during measured runs.
- LED heartbeat continues while the gyro task runs.
- CFSR and HFSR remained zero during measured runs.
- A haltless J-Link read demonstrated why the generation protocol is necessary: interleaved reads are rejected, while stable even generations are accepted.

The exact physical mapping between board rotation and reported gyro axis/sign has not been established. The USART1 path was implemented, but the available J-Link CDC was never wired to PA9, so no session ever captured the UART stream over a physical serial connection. That path is now removed/superseded: live observation of `g_gyro_snapshot` (sequence, init_status, read_error_count, last_read_status, x/y/z) goes exclusively through Ozone/J-Link via the committed project file `apps/rm_c_blinky/ozone/rm_c_blinky.jdebug`, which watches those fields directly and requires no serial hardware.

## Test and Evidence Philosophy

Pure logic should be testable with the host compiler. The current harness covers:

- BMI088 initialization sequencing and configuration values.
- Bus failures, wrong chip IDs, and register-readback failures.
- Burst framing and signed little-endian decoding.
- Scale boundaries and sign behavior.
- Signed and unsigned integer formatting boundaries.
- Coherent snapshot publication and generation wrap.

Tests should use real value objects or in-memory fakes rather than broad mocks. Protocol work should eventually include byte-exact fixtures from official or deployed implementations.

Build success alone is not hardware proof. Hardware claims should identify the binary, flash verification, target voltage, runtime counters, fault registers, timing, and the physical action that produced the observation.

## Safety Boundaries

The current C-board image is intentionally zero-actuation. It does not initialize CAN motor output, PWM actuators, friction wheels, feeder, laser, buzzer, shooter, or leg mechanisms.

The larger project assumes these safety properties before motion is allowed:

- A compile-time actuation gate defaults to disabled.
- A runtime armed state defaults to false.
- Closed gates force zero current/torque at the transport boundary.
- RC loss and stale inter-board data force a safe state.
- Control-task health participates in watchdog and safe-output behavior.
- Dry-run control computation can be observed while transmitted actuation remains zero.
- Motion requires explicit human approval, mechanically safe conditions, and current-limited power.

Physical bring-up evidence should grow from isolated, unloaded mechanisms toward integrated motion. Shooting mechanisms and balance behavior carry the highest consequence and belong behind the strongest evidence gates.

## Scheduling and Concurrency Context

The expected high-rate behavior is hybrid rather than "everything is a normal RTOS task":

- The team gimbal's existing 2 kHz control timing is initially a hardware-timer responsibility.
- The chassis's existing 500 Hz leg-control timing is initially a hardware-timer responsibility.
- CAN ISRs receive, timestamp, and publish into bounded storage; they do not run control logic.
- Vision, referee, HUD, logs, and lower-rate state machines are natural FreeRTOS tasks.
- Lower-rate periodic work can use `vTaskDelayUntil()`.
- Moving high-rate loops into normal tasks requires measured jitter and demonstrated behavioral equivalence.

Any ISR calling a FreeRTOS `FromISR` API must use an NVIC priority compatible with `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`.

## Communication Context

Known inter-board CAN meanings from the team firmware include:

```text
0x010 gimbal -> chassis: RC sticks
0x011 gimbal -> chassis: keyboard and mouse
0x012 chassis -> gimbal: chassis status, shoot speed, capacitor
0x013 gimbal -> chassis: IMU yaw/pitch, shooting, auto-aim
0x014 gimbal -> chassis: vision pitch/yaw/distance
0x120 chassis/power board: battery, capacitor, and load status
```

The long-term architecture expects typed pack/unpack functions, compile-time size checks, byte-exact tests, and explicit stale-data semantics. Application code should not manipulate protocol byte offsets directly. Internal CAN ID `0x120` is unrelated to referee UART command ID `0x0120`.

## Capability Areas Still Open

The following are unresolved project areas, not an ordered task list:

- BMI088 accelerometer, temperature behavior, calibration, and board-frame transform.
- INS/AHRS and physical correlation between board motion and timestamped sensor output.
- DBUS parsing and remote-loss semantics.
- CAN loopback, receive-only feedback, zero-current transmission, utilization, errors, and stale data.
- DJI, LK6010, and DM motor codecs and device abstractions.
- Shared PID, ramp, constrain, deadband, AHRS math, and controller behavior.
- Referee protocol CRC, framing, state, and HUD encoding.
- Vision transport and MiniPC framing.
- Typed robot-link messages between gimbal, chassis, and power systems.
- STM32F405RG chip and board targets for the team gimbal and chassis.
- Preservation and modularization of deployed gimbal modes, chassis modes, leg control, shooting, vision, and HUD behavior.
- Calibration storage, device detection, watchdogs, stack margins, jitter, CAN utilization, and recovery behavior.
- F4 linker hardening and reducing the current broad HAL/LL source glob.

Which of these matters next is a conversation with the user, informed by available hardware, risk, and the immediate robot goal.

## Constraints on Future Decisions

- Do not infer unverified hardware behavior from code alone.
- Do not claim physical axis semantics from stationary gyro noise.
- Do not copy monolithic team firmware into the new architecture without extracting behavior and evidence.
- Preserve team-specific signs, zeros, limits, rates, transitions, and packet bytes until equivalence is demonstrated.
- Keep host tests separate from the ARM cross-toolchain build.
- Keep chip facts independent from board facts.
- Do not enable motion as a side effect of sensor, protocol, or architecture work.
- Do not weaken tests or delete evidence to make a build pass.
- Keep unrelated user changes intact in a dirty worktree.

## How to Use This Context

A future agent should read this file to understand the project, then discuss the user's current intention, priorities, available hardware, and acceptable risk. The document deliberately does not select the next feature or prescribe a continuation sequence.
