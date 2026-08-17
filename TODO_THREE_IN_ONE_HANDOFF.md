# Three-in-One RoboMaster Firmware Handoff

## Core Goal

Build one layered repository containing three independently built and flashed firmware targets:

1. Official RoboMaster Development Board C reference and hardware-validation firmware.
2. Team gimbal firmware for STM32F405RG.
3. Team chassis firmware for STM32F405RG.

The targets must share the same BSP conventions, portable drivers, middleware, protocols, tests, build/flash tools, logging, and validation infrastructure. "Three-in-one" means one repository and one common technical foundation, not one MCU image.

## Source Repositories

### Team firmware behavior sources

- Gimbal: https://github.com/zy-2006-08/Gimbal_26hero_CH010_20260725
- Chassis: https://github.com/zy-2006-08/Chassisl_26_SHANGTAIJIE

The team repositories are the behavioral source of truth for real-vehicle details, including PID parameters, encoder zeros, motor directions, mechanical limits, timing, CAN rates, mode transitions, deploy behavior, leg control, and HUD behavior.

### Official reference sources

- https://github.com/RoboMaster/Development-Board-C-Examples
- https://github.com/RoboMaster/RoboRTS-Firmware
- https://github.com/RoboMaster/referee_serial_port_protocol
- https://github.com/RoboMaster/RoboRTS-Base

Temporary local clones created during the previous session:

- `/var/folders/nd/mjvg9t7x0_79mx_36r4m9qtc0000gn/T/opencode/rm-dev-c`
- `/var/folders/nd/mjvg9t7x0_79mx_36r4m9qtc0000gn/T/opencode/rm-firmware`
- `/var/folders/nd/mjvg9t7x0_79mx_36r4m9qtc0000gn/T/opencode/rm-referee`
- `/var/folders/nd/mjvg9t7x0_79mx_36r4m9qtc0000gn/T/opencode/rm-base`

`dji-sdk/RoboMaster-SDK` is an EP/Tello host-side Python SDK, not the embedded competition-firmware base. Only its Action, subscription, and protocol-layering ideas may be useful.

## Architecture Decision

Keep the existing dependency direction:

```text
app -> board -> chip -> bsp
app -> drivers
app -> middleware
```

Target shape:

```text
apps/
  rm_c_blinky/          # current safe C-board baseline
  standard_robot_c/     # future official reference integration
  gimbal_f405/          # future team gimbal image
  chassis_f405/         # future team chassis image

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

Do not copy either monolithic team `my_main.cpp` directly. First preserve behavior, timing, packet bytes, signs, zeros, limits, and parameters; then extract one module at a time.

## Completed: Phase 1 C-Board Baseline

The repository now supports the official RoboMaster C board as a selectable target.

Implemented:

- STM32F407IGHx Cortex-M4F chip target.
- VFPv4-D16 hard-float ABI.
- 1 MiB Flash, 128 KiB SRAM, and 64 KiB CCM memory map.
- Official C-board 12 MHz HSE to 168 MHz clock tree.
- FreeRTOS V11.1.0 using `GCC_ARM_CM4F`.
- Safe C-board diagnostic app using only:
  - PH10 LED as a normal GPIO.
  - PA9 USART1 TX.
  - Internal TIM6 as the HAL 1 kHz timebase.
- FreeRTOS exclusively owns SysTick.
- Startup assembly is linked only once into the final executable.
- HSE frequency is owned by `boards/rm_dev_board_c/board.cmake`, not the chip layer.
- Stack-overflow and allocation-failure hooks stop safely.
- Build/flash script supports `f103` and `rm_dev_board_c` and fails closed on invalid arguments.
- J-Link script performs `verifybin` before reset/run.
- README documents C-board build and flash commands.

No CAN, motor, PWM actuator, friction wheel, laser, buzzer, IMU, referee, or shooting path is initialized by the current C-board diagnostic image.

### Hardware evidence

J-Link and wiring:

- J-Link S/N: `602712225`
- VTref: approximately 3.28 V
- SWD connected successfully.
- DBGMCU ID: `0x413`
- Cortex-M4/FPU detected.

Final C-board image:

- Path: `build-rm_dev_board_c/apps/rm_c_blinky/rm_c_blinky.bin`
- Size: 14,464 bytes
- SHA-256: `77a82e0fea66187f1991e3cc0876b78a311fe67630785ecb794cd7bca64e802f`

Final HIL result:

- Flash program and verify: PASS
- `g_blink_count`: 9 -> 13 over 2 seconds
- CFSR: `0x00000000`
- RCC_CFGR: `0x0000940A`
- CPU: Thread mode using PSP
- Blink task period: 500 ms

Five review lanes passed with no blockers: goal compliance, hands-on QA, code quality, physical/flash safety, and official-source context.

## Current Worktree State

The Phase 1 work is intentionally uncommitted. Do not reset, checkout, or overwrite it.

Expected status includes:

```text
 M .gitignore
 M CMakeLists.txt
 M README.md
 M bsp/stm32/f4/CMakeLists.txt
 M tools/build_and_flash.sh
?? apps/rm_c_blinky/
?? boards/rm_dev_board_c/
?? bsp/stm32/f4/STM32F407IG_FLASH.ld
?? cmake/chips/stm32f407ighx.cmake
?? TODO_THREE_IN_ONE_HANDOFF.md
```

The C board currently runs `rm_c_blinky`, not the original `user_program.bin`.

Before continuing, run:

```bash
git status --short
bash tools/build_and_flash.sh --no-flash
bash tools/build_and_flash.sh --target rm_dev_board_c --no-flash
```

Do not commit unless explicitly requested.

## Next Phase: Host-Tested Common Core

### P0: Add native C test infrastructure

- [ ] Add `tests/firmware/` using native host GCC and CTest.
- [ ] Keep host tests separate from the ARM cross-toolchain build.
- [ ] Require Given/When/Then test structure.
- [ ] Add byte-exact golden fixtures captured from official/team implementations.

### P0: Port pure modules test-first

- [ ] CRC8 and CRC16 using official referee-protocol vectors.
- [ ] FIFO behavior, wraparound, full, and empty cases.
- [ ] PID update, limit, integral, reset, and golden step response.
- [ ] Ramp, constrain, deadband, and easing functions.
- [ ] AHRS helper math and quaternion-to-Euler conversion.
- [ ] DBUS 18-byte frame parsing using known frames.
- [ ] DJI motor feedback decode and four-current command encode.
- [ ] Referee frame parser with valid, partial, corrupted, and CRC-failure cases.

Do not port official bundled HAL, CMSIS, FreeRTOS, CubeMX generated projects, or CMSIS-RTOS v1 wrappers.

## C-Board Zero-Actuation Bring-Up

Complete these in order and save evidence at every gate:

- [ ] SPI and BMI088 WHO_AM_I.
- [ ] BMI088 raw accelerometer and gyro data.
- [ ] INS/AHRS output; correlate physical board rotation with timestamped logs.
- [ ] DBUS channels, switches, mouse, and keyboard.
- [ ] CAN internal loopback.
- [ ] CAN motor feedback receive only.
- [ ] Zero-current CAN send verification.
- [ ] Referee UART frame parsing.
- [ ] Flash calibration write/reboot/read consistency.
- [ ] Device detect/watchdog table.

Use:

- `tools/build_and_flash.sh`
- `tools/serial_capture.py`
- Camera and Aux-Eye for physical/log correlation
- J-Link memory reads for counters and fault registers

## Shared Module TODO

- [ ] `drivers/can`: HAL-independent bus API and ISR-to-buffer boundary.
- [ ] `drivers/motor/dji`: byte-exact DJI frame codecs.
- [ ] `drivers/motor/lk6010`: LK protocol and feedback.
- [ ] `drivers/motor/dm`: DM command/feedback codec.
- [ ] `drivers/imu/bmi088`: C-board and team-gimbal reusable driver.
- [ ] `drivers/imu/hipnuc`: CH010/HiPNUC parser and continuous yaw.
- [ ] `drivers/imu/jy61p`: chassis IMU driver.
- [ ] `drivers/remote/dbus`: transport-independent DBUS parser.
- [ ] `middleware/controller`: PID, `Gimbal_Zhou`, and `LegCascade`.
- [ ] `middleware/referee`: CRC, parser, state model, and UI packet encoding.
- [ ] `middleware/vision`: MiniPC/CT frame codecs.
- [ ] `middleware/robot_link`: the only definition of inter-board CAN frames.

## Inter-Board Protocol TODO

Implement typed `pack` and `unpack` functions. Application code must not manipulate raw byte offsets directly.

```text
0x010 gimbal -> chassis: RC sticks
0x011 gimbal -> chassis: keyboard and mouse
0x012 chassis -> gimbal: chassis status, shoot speed, capacitor
0x013 gimbal -> chassis: IMU yaw/pitch, shooting, auto-aim
0x014 gimbal -> chassis: vision pitch/yaw/distance
0x120 chassis/power board: battery/capacitor/load status
```

- [ ] Add compile-time size checks.
- [ ] Add byte-exact golden tests.
- [ ] Add timeout and stale-data semantics.
- [ ] Add protocol versioning only if a real compatibility need is identified.
- [ ] Keep internal CAN `0x120` distinct from referee UART command ID `0x0120`.

## Team STM32F405 Targets

### Shared chip target

- [ ] Add `cmake/chips/stm32f405rg.cmake`.
- [ ] Cortex-M4F, VFPv4-D16, hard-float.
- [ ] 1 MiB Flash, 128 KiB SRAM, 64 KiB CCM.
- [ ] Board-specific 25 MHz HSE, 168 MHz system clock.
- [ ] Correct startup and linker script.

### Team gimbal board and app

- [ ] Add `boards/team_gimbal_f405/`.
- [ ] Add `apps/gimbal_f405/`.
- [ ] First gate: LED, UART, FreeRTOS, CAN, and IMU with zero actuation.
- [ ] Port board-specific GPIO, AF, DMA, timers, IRQ priorities, and handles.
- [ ] Preserve original 2 kHz YAW/PITCH timing initially.
- [ ] Port `Gimbal_Zhou`, gimbal modes, turn-around, deploy, friction wheels, feeder, vision, and CH010/BMI088 integration.
- [ ] Preserve original PID values, signs, encoder zeros, limits, and send rates.

### Team chassis board and app

- [ ] Add `boards/team_chassis_f405/`.
- [ ] Add `apps/chassis_f405/`.
- [ ] First gate: LED, UART, FreeRTOS, CAN, and IMU with zero actuation.
- [ ] Port board-specific GPIO, AF, DMA, timers, IRQ priorities, and handles.
- [ ] Preserve initial 500 Hz DM leg-control timing.
- [ ] Port `LegCascade`, balance/remote modes, anti-flip, retract, stair-climb, JY61P, referee HUD, and CAN2 load controls.

## Scheduling Decision

Use a hybrid design initially:

- 2 kHz gimbal control remains hardware-timer triggered.
- 500 Hz DM leg control remains hardware-timer triggered.
- CAN ISR only receives, timestamps, and publishes into bounded buffers.
- Vision, referee, HUD, logs, and lower-rate state machines run as FreeRTOS tasks.
- Low-rate housekeeping uses `vTaskDelayUntil()`.
- Do not move high-rate loops into normal RTOS tasks until jitter is measured and behavior equivalence is demonstrated.

Any ISR calling a FreeRTOS `FromISR` API must have an NVIC priority numerically equal to or lower urgency than `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`.

## Physical Safety Gates

- [ ] Add compile-time `RM_ACTUATION_ENABLED`, default `0`.
- [ ] Add runtime `actuation_armed`, default `false`.
- [ ] When either gate is closed, all motor CAN currents/torques must be forced to zero at the transport boundary.
- [ ] RC loss forces zero output.
- [ ] Inter-board timeout forces zero output or safe mode.
- [ ] Control-task health failure prevents watchdog feed and forces safe output.
- [ ] Verify dry-run computed currents may be nonzero while transmitted currents remain zero.
- [ ] Do not enable motion until explicit human approval, motors are mechanically safed, and power is current-limited.

First powered sequence:

1. Single motor, unloaded.
2. Small command.
3. Verify direction and feedback.
4. Disconnect RC and verify immediate zero.
5. Four-wheel chassis.
6. Single gimbal axis.
7. Dual-axis gimbal.
8. Single leg.
9. Dual-leg balance.
10. Shooting mechanisms last.

## Verification Metrics

Do not accept "looks correct". Capture:

- Control-period jitter, especially 2 kHz and 500 Hz loops.
- CAN utilization, error frames, queue overruns, and stale feedback.
- IMU sample age and update rate.
- FreeRTOS stack high-water marks.
- Gimbal steady-state error, overshoot, and oscillation.
- Leg mode-transition timing and failsafe behavior.
- Referee HUD loss/recovery behavior.
- Binary hash tied to each HIL log.

## Known Non-Blocking Hardening

- [ ] Reserve explicit MSP space and add heap/stack collision assertions in F4 linker scripts before enabling libc allocation.
- [ ] Rename CCMRAM as explicit no-init or add copy/zero startup semantics before placing data there.
- [ ] Replace the full F4 HAL/LL source glob with an explicit enabled-module source list to reduce clean-build time.
- [ ] Address the pre-existing FreeRTOS `pxVectorTable` unused warning.
- [ ] Update stale README references to missing `tools/setup_macos.sh` and `tools/jlink/flash_app.sh` when doing repo documentation cleanup.

## Continuation Command

In a new session, use:

```text
Read TODO_THREE_IN_ONE_HANDOFF.md and continue from Phase 2. First verify the uncommitted Phase 1 work, then add the host firmware test harness and port CRC/FIFO/PID test-first. Keep the C board zero-actuation.
```
