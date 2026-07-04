# STM32 启动文件修复（bare metal + newlib-nano）

## 问题

ST 官方启动文件在 bare metal + newlib-nano 环境下有两个坑：

1. `__libc_init_array` 调用的 `_init` 没有返回指令，CPU 跑飞
2. `Default_Handler` 是死循环，任何意外中断都会卡死 CPU

## 修复方法（三处改动）

### 1. Reset_Handler 开头加 `cpsid i`（禁用全局中断）

```asm
Reset_Handler:
  cpsid i              ← 加这一行
  bl  SystemInit
```

### 2. Default_Handler 死循环 → 直接返回

```asm
; 改前
Default_Handler:
Infinite_Loop:
  b Infinite_Loop       ← 死循环

; 改后
Default_Handler:
  bx lr                 ← 直接返回
```

### 3. 注释掉 `__libc_init_array` 调用

```asm
; 改前
  bl  __libc_init_array

; 改后
  nop                       @ __libc_init_array skipped
```

## 适用芯片

| 芯片系列 | 启动文件 | 修法是否一致 |
|---------|---------|------------|
| STM32F103 | startup_stm32f103x*.s | ✅ 完全一样 |
| STM32F407 | startup_stm32f407xx.s | ✅ 完全一样 |
| GD32F4 | startup_gd32f4xx.s | ✅ 完全一样 |
| 其他 Cortex-M | *.s 启动文件 | ✅ 修法通用 |

## 我们的模板

已修好的启动文件在：
- `stm32_template/app/startup/startup_stm32f103xb.s`

以后新项目直接复制这个文件，不需要再修。
