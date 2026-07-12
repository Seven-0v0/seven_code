# apps/ — 应用层（项目/产品）

## 这一层放什么

每个具体项目/产品一个子目录，例如 `apps/blink_demo/`、`apps/sensor_node/`。
一个 app = **选一块板子（board）** + **产品业务逻辑**（main、任务编排、应用状态机、协议实现等）。

## 命名约定

- 每个 app 一个独立子目录，目录名用小写下划线：`apps/<product_name>/`
- 每个 app 子目录含自己的 `CMakeLists.txt`、`src/`、可选 `include/`
- app 在其 CMake 中显式声明「选用哪块板子」（引用 `boards/<board>`），而不是直接写死芯片型号

## 职责边界（该放 / 不该放）

放这里：
- `main.c` / 应用入口
- 产品级任务（业务 task）、状态机、上层协议、UI 逻辑
- app 私有的、不打算复用的代码

不放这里：
- 芯片编译宏/CPU 选项/链接脚本 → 属于 `cmake/chips/`
- 引脚映射/时钟/FreeRTOSConfig.h → 属于 `boards/`
- 可移植设备驱动（传感器、屏幕）→ 属于 `drivers/`
- FreeRTOS 内核等共享中间件 → 属于 `middleware/`
- 厂商外设库/启动文件 → 属于 `bsp/`

## 与相邻层的关系（依赖方向）

```
app → board → chip(cmake/chips) → bsp
app → drivers
app → middleware(FreeRTOS，由 board 注入配置、app 链接)
```

app 是稳定性梯度里**最不通用、最易变**的一层，位于货架最顶层。
它只向下依赖，绝不被任何下层反向依赖。
