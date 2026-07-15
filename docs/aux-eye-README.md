# 辅眼（aux-eye）使用指南

> 面向使用者的中文说明文档。技术规格见 `docs/aux-eye-perception.md`；观测 JSON 的字段定义见 `docs/aux-eye-perception.schema.json`。

## 1. 这是什么，为什么需要它

设备的串口遥测（主眼）能精确、高频地上报自己算出来的内部状态：位置、速度、姿态角、状态字。但这些数字都来自设备内部的传感器融合，反映的是"设备自己以为发生了什么"，不是物理世界的真实画面。

辅眼补的是主眼测不到的那一层：设备相对地面的真实朝向、有没有偏离预期位置、某个部件是不是到了该有的角度、有没有倾倒或被遮挡、肉眼能看到的抖动或晃动。这些都是通用的场景观察能力，不绑定任何具体设备构型——辅眼看到的是"姿态、位置、几何、失败、振荡"这类通用概念，具体设备是什么，由观测时的场景决定。

两者分工很清楚：主眼负责"设备自己报的数字准不准、快不快"，辅眼负责"画面里实际发生的事，和数字报的是不是一回事"。

## 2. 现阶段已验证能力

以下每一项都有真实运行记录，证据在 `.omo/evidence/final-e2e-verification.txt`：

| 能力 | 验证方式 | 结果 |
| --- | --- | --- |
| 摄像头枚举 | `camera_capture.py --list` | 识别到 3 个设备（MacBook Air 相机 / UGREEN Camera / iPhone 连续互通相机），index 1 = UGREEN USB 摄像头 |
| 单帧实时抓拍 | index 1 抓 1 帧 | 成功，NDJSON 里的 sha256 与文件重新计算的 sha256 完全一致，分辨率 1280x720 |
| 连续多帧抓拍 | `--frames 3 --interval 0.5` | 3 帧全部落盘，3 个文件、3 个互不相同的哈希 |
| 观测校验器（合法观测） | `verify_aux_eye.py` 对一份合法观测 | 通过，输出"observation PASSED all gates" |
| 观测校验器（被篡改的观测） | 把 `source_frame_sha256` 改成全零再校验 | 正确拒绝，退出码 1，输出里含 "sha256" 字样说明拒绝原因 |
| ArUco 可选定量姿态 | 对已知 45° 偏航的标记图跑 `aruco_pose.py` | 测出 44.96°，落在允许范围 [43°, 47°] 内 |
| 自动化测试套件 | `pytest tests/aux-eye -q` | 12 项全部通过 |
| 固件/主眼零改动 | `git diff --stat` 对比固件相关目录 | 无任何改动，辅眼是纯新增，不动现有链路 |

**AI 真实读图能力**：早期执行会话读不出图片内容，报 `does not support image input`。这个问题已经解决，原因和修复方式见第 4 节。修复后，AI 已能正确识别真实摄像头帧里的窗帘、人物、手势、环境等内容。

## 3. 当前边界（现在做不到的事）

辅眼是"抓一帧、看一帧"的感知层，不是持续监控系统。具体来说：

- **不是实时视频流**。每次调用都是离散地抓 1 帧或若干帧，帧与帧之间没有视频编解码，也没有持续的画面流。
- **没有跨帧时序分析层**。多帧抓取（`--frames N`）只是把 N 张独立的帧存下来，辅眼本身不会把它们拼成一条"变化时间线"去分析趋势（比如"这几帧里晃动越来越大"）。这类分析目前需要人或 AI 自己看多张图对比，工具不代劳。
- **不是准实时的边抓边分析循环**。没有"每隔 X 秒自动抓一帧并自动出结论"的常驻进程；每次抓取和每次分析都是显式调用。
- **没有接入任何整定/控制闭环**。辅眼只产出观测 JSON，不会拿这份观测去反过来调整任何设备参数（比如 PID 参数）。把观测接入决策闭环是尚未开始的后续工作。
- **ArUco 姿态是可选增强，不是默认路径**。默认路径完全靠 AI 读图；只有画面里恰好有 ArUco 标记、且 OpenCV 的 ArUco 检测器能识别出这个标记时，才会额外产出一份定量姿态数据。标记太小、太模糊或角度太斜可能导致检测不到，此时返回 `detected: false`，不会影响默认的 AI 读图路径。
- **图像内容字段是"能填就填，填不了就留空"**，不是保证一定有值。`scene.subject`、`objects`、顶层 `confidence` 都是可选字段：在没有视觉能力的会话里它们会自然是空字符串/空数组，而不是被硬编出来；只有 `source_frame_sha256`、`visible`、`failure_reason` 这三个和"帧交接"相关的字段是必填的。

## 4. OpenCode 视觉模型配置（一次性，已完成）

辅眼依赖执行 agent 具备真实的图像输入能力。如果换了新的 OpenCode 环境或换了新模型，需要确认这一步配置到位，否则会重现"能抓帧、但 AI 读不出图"的问题。

**根因**：自定义 provider 下的模型如果没有在配置里声明 `attachment` 和 `modalities`，OpenCode 会在把请求发给模型之前，就在客户端本地判定这个模型不支持图片输入，直接拦截并报错 `does not support image input`——这个错误发生在到达模型之前，跟模型本身能不能读图无关，纯粹是本地配置缺失导致的误判。

**修复方式**：在模型配置里，给每个需要支持图片的模型加上两个字段：

```jsonc
{
  "attachment": true,
  "modalities": {
    "input": ["text", "image"],
    "output": ["text"]
  }
}
```

`attachment: true` 告诉 OpenCode 这个模型接受文件附件；`modalities.input` 里的 `image` 告诉 OpenCode 这个模型接受图片模态。两个字段缺一都会导致图片被拦截。

**改完之后必须完整重启 OpenCode**（不是重开一个会话，是重启整个进程），配置才会重新加载生效。修改配置或重启前建议先看一下当前正在跑的会话，避免中断未保存的工作。

配置文件路径不写在这里（不同机器可能不同），改的时候按自己环境里 OpenCode 的配置文件位置操作即可；重点是记住"缺 `attachment`/`modalities` → 报 does not support image input → 补上两个字段 → 完整重启"这条排障路径。

## 5. 环境准备

只需要装一次：

```bash
python3 -m pip install -r tools/requirements-vision.txt
```

`tools/requirements-vision.txt` 里只有 `opencv-contrib-python`、`numpy`、`jsonschema`、`qrcode[pil]` 这几个包。**不要额外装 `opencv-python`**——它和 `opencv-contrib-python` 会互相冲突，同时装了会坏环境。

首次在 macOS 上用摄像头，系统会弹一次权限请求，需要在"系统设置 → 隐私与安全性 → 相机"里给当前终端授权。这一步是一次性的，之后不用再重复处理。

## 6. 使用方法

### 第一步：列出摄像头

```bash
python3 tools/camera_capture.py --list
```

stderr 会打印找到的摄像头列表（index + 名字），stdout 打印可被脚本消费的裸 index。**macOS 上摄像头 index 可能会漂移**：接上新设备、开了 Zoom/OBS 之类虚拟摄像头、iPhone 连续互通相机被激活，都可能改变 index 的顺序。如果不确定当前 index，改用 `--name` 按名字子串选择，比如：

```bash
python3 tools/camera_capture.py --name UGREEN --frames 1
```

这样即使 index 变了，也能稳定选中同一台物理摄像头。

### 第二步：抓帧

单帧：

```bash
python3 tools/camera_capture.py --index 1 --frames 1 --timeout 20
```

连续多帧（比如每 0.5 秒抓一帧，共 10 帧）：

```bash
python3 tools/camera_capture.py --index 1 --frames 10 --interval 0.5 --timeout 60
```

stdout 会输出 NDJSON，每帧一行，形如：

```json
{"path": ".omo/evidence/frames/20260716-002852/0-20260716-002853-551493.jpg", "sha256": "b18c96...", "index": 0, "ts": "20260716-002853-551493", "w": 1280, "h": 720}
```

`path` 是帧文件的位置，`sha256` 是这个文件的哈希，后面校验时要用到。

### 第三步：交给 AI 分析

把 `path` 指向的帧文件交给 AI（用 `look_at` 或等价的多模态读图能力），让它产出一份符合 `docs/aux-eye-perception.schema.json` 的观测 JSON。`source_frame_sha256` 字段必须填第二步 NDJSON 里那个 `sha256` 的值，这样才能证明这份观测确实对应刚抓的那一帧，不是张冠李戴。

一份合法观测的样子：

```json
{
  "source_frame_sha256": "b18c965b1f035a524ba22ecd916072608f01ebeb9a25910f9f0729dc4b81165c",
  "visible": true,
  "failure_reason": "none",
  "scene": { "subject": "a desk with a laptop and a cup" },
  "objects": [
    { "name": "laptop", "confidence": 0.9 },
    { "name": "cup", "confidence": 0.7 }
  ],
  "confidence": 0.8
}
```

### 第四步：校验观测

```bash
python3 tools/verify_aux_eye.py --frame .omo/evidence/frames/20260716-002852/0-20260716-002853-551493.jpg --observation observation.json
```

观测 JSON 也可以用 `-` 从 stdin 读入。校验器会做三件事：schema 校验（字段类型、必填项、enum 取值都合法）、帧身份比对（重新计算帧文件的 sha256，跟观测里的 `source_frame_sha256` 比对）、字段逻辑一致性（`visible=true` 时 `failure_reason` 必须是 `none`，`visible=false` 时必须给出真实原因）。全部通过退出码 0，任何一项不过退出码 1 并打印具体失败原因；校验器不检查描述文字写得准不准，只判定结构和交接是否正确。

### 可选：ArUco 定量姿态

如果画面里贴了一个 ArUco 标记（且标记足够清晰、可被 OpenCV ArUco 检测器识别），可以额外跑：

```bash
python3 tools/aruco_pose.py --frame <帧路径>
```

输出形如 `{"marker_id": 23, "yaw_deg": 44.96, "detected": true}`，给出相对相机平面法线的偏航角。具体行为分三种情况：没有检测到任何标记时，输出 `detected: false`，`marker_id` 和 `yaw_deg` 都是 `null`；检测到了标记但姿态解算失败时（比如角点几何退化），`marker_id` 会保留检测到的值，`yaw_deg` 为 `null`，`detected` 仍为 `true`；两种情况都不会报错崩溃，也都不会影响默认的 AI 读图路径——这纯粹是个可选的加分项。

## 7. 目录结构

```
tools/
  camera_capture.py          抓帧工具（纯采集，不含任何感知/网络逻辑）
  verify_aux_eye.py          观测校验器（schema + 帧身份 + 字段逻辑）
  aruco_pose.py               可选 ArUco 定量姿态检测
  requirements-vision.txt    依赖清单（只装 contrib 版 opencv）
docs/
  aux-eye-perception.md              感知规格（面向执行 agent 的技术说明）
  aux-eye-perception.schema.json     观测 JSON 的唯一字段定义来源
  aux-eye-README.md                  本文档
tests/
  aux-eye/                    pytest 测试套件
  fixtures/aux-eye/            测试用固定图片（明亮场景图、纯黑帧、45° ArUco 图等）
.omo/evidence/
  frames/<runid>/              每次抓帧的产物（已 gitignore，不入库）
  final-e2e-verification.txt   最近一次全流程端到端验证记录
```

## 8. 排障

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| AI 报 `does not support image input` | OpenCode 里这个模型没配 `attachment`/`modalities` | 按第 4 节补上配置，完整重启 OpenCode |
| `camera_capture.py --list` 输出 `[SKIP][cam] no cameras detected`，退出码 2 | 当前环境没有可用摄像头 | 检查摄像头是否插好/系统是否识别；这是设计好的行为，不是 bug |
| 抓帧报 `[ERR][cam]` 并提示检查权限 | macOS 没给终端相机授权，或读到的是黑帧 | 去"系统设置 → 隐私与安全性 → 相机"里给终端授权，重新运行 |
| 抓到的帧总是别的摄像头（比如误抓到 iPhone 相机） | index 漂移，插了新设备或有虚拟摄像头 | 先跑 `--list` 确认当前 index，或者直接用 `--name UGREEN` 之类的子串按名字选 |
| `pip install` 报 opencv 相关冲突或导入失败 | 同时装了 `opencv-python` 和 `opencv-contrib-python` | 卸掉 `opencv-python`，只保留 `opencv-contrib-python`：`pip uninstall opencv-python` |
| `verify_aux_eye.py` 报 sha256 不匹配 | 观测里的 `source_frame_sha256` 填错了，或者引用了别的帧 | 重新核对 NDJSON 里那一帧的 `sha256` 字段，确保填的是同一次抓帧的值 |
| `aruco_pose.py` 一直 `detected: false` | 画面里没有标记，或标记太小/太模糊/角度太斜导致 OpenCV 检测器识别不出来 | 换一个更大、更清晰、角度更正、四周留有足够空白（quiet zone）的标记再试；这本来就是可选功能，不影响默认路径 |

## 9. 安全提示

- 抓帧产物（`.omo/evidence/frames/`）已经在 `.gitignore` 里排除，不会进入版本库。测试用的固定图片（`tests/fixtures/aux-eye/`）是刻意保留入库的基线素材，不是抓帧产物。
- 如果 AI 分析这一步用到了需要 API key 的模型服务，key 应该通过环境变量引用，不要直接写进配置文件或代码里；已经使用过的 key 建议定期轮换。
- `camera_capture.py` 本身不发起任何网络请求，纯本地抓帧存盘；AI 分析这一步是否联网、联的是哪个服务，取决于你使用的模型和 provider 配置，不在这个工具的控制范围内。

## 10. 下一步

以下是感知层完成后，还没开始做的后续工作：

- 跨帧时序对比层：把多帧观测排成一条时间线，输出类似"晃动趋势在变大/变小"这样的变化判断，而不是只看单帧快照。
- 准实时边抓边分析循环：目前每次都是显式调用抓帧和分析，还没有"自动定时抓、自动出结论"的常驻流程。
- 接入真实设备的整定/控制决策闭环：把辅眼观测真正用来影响设备参数调整，目前完全没有做。
