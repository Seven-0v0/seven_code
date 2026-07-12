# stm32_template（已废弃外壳）

**此目录仅为历史保留，主要开发已迁移至仓库根的货架化结构。**

## 当前状态

- ✅ **已迁出**: app、Middlewares、cmake、common → 新货架结构（见仓库根 README）
- ⚠️ **已搁置**: `bootloader/`（GD32 bootloader 原型，暂不活跃维护，保留供参考）

## 新开发流程

请使用仓库根的货架化结构：

```
/Users/seven.xu/code/
├── apps/blinky_f103/      # 新项目在此开发
├── boards/                # 板级配置
├── bsp/                   # 芯片 BSP
├── middleware/            # FreeRTOS 等中间件
└── cmake/                 # 构建配置
```

详细说明见仓库根 `README.md` 和 `docs/ai-workflow.md`。
