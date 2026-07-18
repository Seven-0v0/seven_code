# Aux-Eye 周期性采样巡检协议

## 1. 定位与边界

本协议定义由 agent 执行的周期性采样巡检。Python 工具只完成采集、状态持久化和确定性校验，不调用多模态 agent，也不构成长驻进程。辅眼只检测和结构化上报物理事件，可以否决、分类、请求追加观测或停止。任何固件参数改动都必须在决策记录的 `parameter_change_basis.serial_evidence` 中引用串口证据。

相机每次采样都要打开、预热、抓帧和关闭，随后还要逐帧分析。因此采样频率低于连续视频。持续事件在本协议的期限假设下有检测上界，短于采样间隔的瞬时事件可能完全落在两个窗口之间，**无检测保证**。

## 2. 调用契约

项目内入口为 `.opencode/skills/aux-eye-monitor/SKILL.md`。harness 调用形态如下：

```text
skill(name="aux-eye-monitor", user_message="start --goal goals/inspection.json --camera-name UGREEN --serial-device /dev/tty.usbmodem1101 --baud 115200 --runid inspection-001")
skill(name="aux-eye-monitor", user_message="resume --runid inspection-001")
skill(name="aux-eye-monitor", user_message="status --runid inspection-001")
skill(name="aux-eye-monitor", user_message="abort --runid inspection-001")
```

`start` 创建权威 run-state 并进入周期。`resume` 先执行 `resume-check`，按 `cycle_phase` 继续。`status` 只读取状态。`abort` 进入 `safe_abort`。运行证据位于 `.omo/evidence/aux-eye-monitor/<runid>/`。

## 3. 默认值与周期超时

目标文件省略可选限制时，skill 使用以下协议默认值：

| 设置 | 默认值 | 含义 |
| --- | ---: | --- |
| `max_iterations` | 20 | 单次运行的最大周期数 |
| `stability_windows` | 2 | 宣告收敛前连续达标窗口数 |
| `required_confirmations` | 2 | 持续事件进入决策所需连续确认数 |
| `serial_silence_s` | 30 | 无合法串口日志的累计上限 |
| `max_consecutive_resets` | 2 | 允许的连续启动横幅计数 |
| `visibility_loss_windows` | 2 | 允许的连续不可见窗口数 |
| `cycle_deadline_s` | 180 | 从 `building` 到 `advance` 的强制单周期期限 |

`cycle_deadline_s` 包含构建和烧录、相机 `--timeout 15`、最多三帧分析、串口窗口和确定性 helper。`set-phase --phase building` 在权威 state 中记录单周期的单调起点；skill 在每个有界或不可逆步骤前后运行 `aux_eye_run_state.py deadline-check --runid <id>`。任何阶段超过 180 秒时，该 gate 原子回收已验证身份的后台串口，记录 `cycle_deadline_exceeded`，并以 `needs_human` 终止，不继续烧录或推测结果。

持续事件的最大检测延迟按下式计算：

```text
cycle_deadline_s × (required_confirmations + stability_windows)
```

使用默认值时，上界为 `180 × (2 + 2) = 720` 秒，也就是 12 分钟。这个上界建立在事件持续存在、每个周期都在 180 秒内完成、相机可用且分析服务在期限内返回的假设上。它不是瞬时事件保证。

## 4. 触发

周期有两个触发来源：

1. `advance` 返回 `continue` 或 `candidate_success` 后开始下一个定时采样周期。
2. `tools/aux-eye/serial_anomaly_scan.py` 检出 `[ERR]`、`[FATAL]` 或目标谓词异常时，在当前周期内请求视觉核实。异常不会绕过相位、身份、历史或决策校验门。

同一 runid 同时只能有一个活动周期。达到终态后不再接受写操作。

## 5. 相机预检与身份核对

首次运行先执行 `python3 tools/camera_capture.py --list`，从 stderr 的索引与名称列表确认目标名称，再使用 `--name UGREEN` 抓取预检帧。把实际索引和完整名称写入 run-state。

每个周期抓帧前再次执行 `--list`，检查保存的 `camera_index` 仍映射到同一 `camera_name`。抓帧命令继续使用 `--name`，并检查每行 NDJSON 的 `index` 等于本周期重新映射的索引。名称缺失、索引漂移或 NDJSON 索引不符均进入 `needs_human`，原因为 `camera_identity`。

## 6. 烧录前串口与 envelope 时间对齐

串口采集必须在 flash 前启动，并保持跨复位运行。stdout 和 stderr 分别写文件。登记前用 `capture-identity --pid <pid>` 获取稳定 token，再以 `set-serial-capture --pid <pid> --identity <token>` 存入 PID、token 和两个路径。readiness 与回收都会重新验证 PID 加 token；有效 state 的 PID 复用或 token 不匹配时只清除登记，绝不发信号。遗留或畸形 state 被闭合集合校验拒绝。唯一 readiness 条件是身份仍匹配且 stderr 含 `[OK][serial] capturing from`。串口数据内容不参与 readiness 判断。

时间对齐只保证 envelope 级语义：串口 envelope 从后台采集进程成功打开设备开始，覆盖 `flash_started`、复位、相机窗口和决策；相机 NDJSON 给出每帧时间戳。`serial_capture.py` 没有逐行时间戳，所以协议不声称亚秒级因果对齐。每轮证据要记录串口进程启动时间、`flash_started` 落盘时间、帧时间戳和周期结束时间。

## 7. 一个监控周期

相位顺序固定为：

```text
building -> serial_started -> flash_started -> flashed -> captured -> evaluated -> decided
```

1. `set-phase --phase building`，完成本轮允许的代码改动，并在每个有界或不可逆步骤前后运行 `deadline-check --runid <id>`，再运行 `bash tools/build_and_flash.sh --no-flash`。构建失败时进入 `safe_abort`；deadline gate 自己终止为 `needs_human/cycle_deadline_exceeded`。
2. `set-phase --phase serial_started`，后台启动 `serial_capture.py`，登记 PID、stdout、stderr，并等待 `set-serial-ready` 成功。启动超时或进程退出时进入 `needs_human`。
3. 在真正烧录前原子执行 `set-phase --phase flash_started`，然后运行 `bash tools/build_and_flash.sh`。成功后先 `set-flash-done`，再 `set-phase --phase flashed`。恢复时发现 `flash_started` 且 `build_flash_done=false`，必须以 `flash_interrupted` 进入 `needs_human`，不得猜测或重复烧录。
4. 执行每周期相机索引到名称检查，使用 `camera_capture.py --name` 采集最多三帧，将 NDJSON 保存到周期目录，然后 `set-phase --phase captured`。
5. agent 对每帧执行图像读取，生成封闭结构的 temporal observation。第一周期调用 `tools/aux-eye/verify_aux_eye_temporal.py`；所有非第一周期都必须传 `--history`，指向按时间排序的既往周期目录。验证失败时进入 `needs_human`。
6. 执行 `tools/aux-eye/serial_anomaly_scan.py`，用独立的 `^\[BOOT\]` 计数更新 reset 状态，再执行 `tools/aux-eye/aux_eye_goal_decide.py`。goal helper 读取旧 `stability_count`，用“旧计数加当前窗”计算 `converged`。完成后 `set-phase --phase evaluated`。
7. **先得到 goal_decide 的当前窗结果，再更新 stability，再调用 next-action。** predicate 分支以 `goal_predicate_result` 为当前窗结果，agent_judgment 分支以 `agent_window_ok` 为当前窗结果。true 执行 `incr-stability`，false 或 indeterminate 执行 `reset-stability`。绝不能根据 `converged` 更新计数。
8. 先设置 `set-phase --phase decided`，再执行 `next-action --converged <bool> --window-ok <bool> --camera-index-now <int> --camera-name-now <name>`，把唯一 action 持久化并写入决策记录。通过 `tools/aux-eye/verify_aux_eye_decision.py` 后才执行 `advance --action <action>`。正常的 `continue`、`candidate_success` 和 `success` 必须在 `decided` 后由 `next-action` 产生；相机身份、阈值、迭代耗尽等 fail-closed 原因仍可在更早阶段产生 `needs_human`。
9. 正常 cycle 的 `advance` 原子回收登记且身份仍匹配的 PID，随后才写下一周期或终态，不能在 terminal `advance` 后再调用 mutating cleanup。异常发生在 advance 前时可调用 `clear-serial-capture`；恢复时若 PID 已死或身份不匹配，只清除登记并按当前 phase 安全重开。遗留或畸形 state 被闭合集合校验拒绝，必须人工新建运行；不得跨越 `flash_started` 中断门。

## 8. 终态机

| action | 终态 | 条件与下一步 |
| --- | --- | --- |
| `continue` | 否 | 当前窗未达标，`advance` 回到 `building` |
| `candidate_success` | 否 | 当前窗达标但尚未收敛，继续下一周期 |
| `success` | 是 | 仅 `converged=true` 且 pending action 匹配时可达 |
| `safe_abort` | 是 | 操作者中止或构建等可控失败，不再执行周期 |
| `needs_human` | 是 | fail-closed 原因触发，需要人工处理后新建运行 |

`next-action` 是 action 的唯一正常来源。`advance` 必须匹配持久化的 pending action。终态后的 mutating 子命令必须失败。

## 9. fail-closed 条件

以下任一原因都禁止继续盲目迭代：

| 原因 | 判定 | action |
| --- | --- | --- |
| 串口静默 | 合法日志为零且累计达到 `serial_silence_s` | `needs_human` |
| 重复复位 | `^\[BOOT\]` 计数大于 `max_consecutive_resets` | `needs_human` |
| 可见性丢失 | 连续 `visible=false` 达到 `visibility_loss_windows` | `needs_human` |
| 相机身份漂移 | 当前索引与保存名称不再映射，或 NDJSON index 不符 | `needs_human` |
| 证据冲突 | 串口目标结果与同窗物理失败事件冲突 | `needs_human`，决策使用 `aux_role=veto` |
| 迭代耗尽 | `iteration >= max_iterations` | `needs_human` |
| 周期超时 | `deadline-check` 发现从 `building` 起超过 `cycle_deadline_s` | `needs_human/cycle_deadline_exceeded` |
| 烧录中断 | 恢复时为 `flash_started` 且未记录完成 | `needs_human` |

事件 `confirmations` 必须由 temporal verifier 结合历史重算。首窗只能为 0 或 1。机器门可以证明结构、帧身份、顺序和证据链一致，不能证明视觉语义必然正确。

## 10. 与 ai-workflow 交接

每轮写入经验证的 temporal observation、串口捕获、goal 结果和 decision record。现有开发闭环只消费通过校验的决策记录。`firmware_parameters_changed=true` 时，`parameter_change_basis.serial_evidence` 必须非空，并使用路径、行号和行 SHA-256 的封闭引用；aux event 不得进入该对象。辅眼不提供参数值或参数增量。

## 11. Benchmark 方法与假设

`.omo/evidence/task-7-benchmark.txt` 保存可复现命令、环境和结果。基准只测本地确定性 helper 的进程启动、JSON 解析、schema 校验和状态文件 I/O。它不测构建、硬件烧录、相机或逐帧分析。

当前外接相机的两次有界尝试都没有产生可用帧，因此没有相机或视觉分析耗时成功数据。`cycle_deadline_s=180` 是带保护的协议预算，不是本机端到端实测值。预算假设为：构建和烧录 45 秒，相机 15 秒，三帧分析共 90 秒，串口窗口与确定性 helper 共 30 秒。串口采集与其覆盖的阶段并行，期限仍从 `building` 的 envelope 起点统一计算。

## 12. Predicate grammar

以下 BNF 和真值表是谓词契约。实现不得改变其语义。

BNF:
```
expr        := or_expr
or_expr     := and_expr ( "||" and_expr )*
and_expr    := not_expr ( "&&" not_expr )*
not_expr    := "!" not_expr | atom
atom        := "(" expr ")" | agg | comparison
agg         := ("any"|"all") "(" agg_comparison ")"
agg_comparison := "events." KIND "." AGG_FIELD op literal      // 恰一个 event-kind 字段 vs 字面量
comparison  := operand op operand
operand     := serial_ref | event_scalar | literal
serial_ref  := "serial." IDENT
event_scalar:= "events." KIND "." SCALAR_FIELD                 // 单值访问(不含 count 进聚合)
op          := ">" | ">=" | "<" | "<=" | "==" | "!="
KIND        := orientation_change | oscillation | position_drift | part_geometry | out_of_frame | occluded
AGG_FIELD   := status | trend | start_frame | end_frame | confirmations
SCALAR_FIELD:= status | trend | start_frame | end_frame | confirmations | count
literal     := number | string
number      := ["-"] DIGIT+ ["." DIGIT+]         // 十进制;无指数/无前导小数点/无 NaN/Inf
string      := '"' ( CHAR | '\\"' | '\\\\' )* '"'   // 仅 \" 和 \\ 两种转义
CHAR        := 除 " 与 \ 外的任意字符          // 裸反斜杠非法
WS          := 字符串外的空白一律忽略(token 之间)
IDENT       := [A-Za-z_][A-Za-z0-9_]*
```
求值规则(写死):
- **聚合参数限一个 kind vs 字面量**:`agg_comparison` 左侧必须是 `events.KIND.AGG_FIELD`、右侧必须是 literal(禁 var-vs-var、禁跨 kind、禁 `count` 进聚合;违反=语法错误→exit2)。
- 普通 `comparison` 允许 serial_ref / event_scalar 与 literal 比较;**禁 var-vs-var**(两侧都是 var → 语法错误→exit2)。
- `serial.<key>`:从 key=value 取字符串原值。比较时**类型强制规则**:若 op∈{>,>=,<,<=} 或两侧任一为 number literal → 两侧都尝试转 float,转失败该叶子=false;若两侧都当字符串(op∈{==,!=} 且对方是 string literal)→ 字符串比较。即 `serial.x=="1"` 按字符串;`serial.x==1` 按 float(`"1"`,`1`,`1.0` 在 float 语义下相等)。
- `events.<kind>.SCALAR_FIELD`(单值,含 count):count=该 kind 事件条数(恒可取);其余字段要求该 kind 恰 1 条,否则(0/多条)叶子=false。
- `any(agg_comparison)`/`all(...)`:对该 kind 每条事件求内部比较,any=或/all=与;**空集合 any=false、all=true**。
- 缺失变量 / 类型强制失败 / 类型不符:该**叶子** = false(含 `!=`:`serial.missing != "x"` = false);`!` 作用其上:`!(false)`=true。
- 语法错误 / 非白名单 token / var-vs-var / 跨kind聚合 / count 进聚合:拒绝 → 调用方 exit 2。

真值表(≥12 行,namespace 简写 s=serial, e=events):

| # | 表达式 | namespace | 预期 |
|---|--------|-----------|------|
| 1 | `serial.uptime > 0` | s.uptime=5 | true |
| 2 | `serial.uptime > 0` | s.uptime=0 | false |
| 3 | `serial.missing > 0` | {} | false(缺失叶子) |
| 4 | `!(serial.missing > 0)` | {} | true(取反缺失) |
| 5 | `serial.mode == "run"` | s.mode="run" | true |
| 6 | `serial.mode == "run"` | s.mode="idle" | false |
| 7 | `events.oscillation.count >= 1` | e.oscillation=[{}] | true |
| 8 | `events.oscillation.count >= 1` | e.oscillation=[] | false |
| 9 | `events.oscillation.status == "detected"` | 恰1条 status=detected | true |
| 10 | `events.oscillation.status == "detected"` | 2条(多条单值访问) | false(叶子=false) |
| 11 | `any(events.oscillation.confirmations >= 2)` | 两条 confirmations=[1,3] | true |
| 12 | `all(events.oscillation.confirmations >= 2)` | 两条 confirmations=[1,3] | false |
| 13 | `any(events.position_drift.status == "detected")` | 空集 | false(any 空) |
| 14 | `all(events.position_drift.status == "detected")` | 空集 | true(all 空) |
| 15 | `serial.a > 0 && any(events.oscillation.trend == "increasing")` | s.a=1, 一条 trend=increasing | true |
| 16 | `serial.x >` | 任意 | exit2(语法错误) |
| 17 | `serial.v == 1` | s.v="1.0" | true(数值强制:1.0==1) |
| 18 | `serial.v == "1"` | s.v="1.0" | false(字符串比较:"1.0"!="1") |
| 19 | `serial.missing != "x"` | {} | false(缺失叶子,含 !=) |
| 20 | `serial.t >= -2.5` | s.t=-2.5 | true(负号/小数字面量) |
| 21 | `serial.name == "a\"b"` | s.name=`a"b` | true(\" 转义) |
| 22 | `any(events.oscillation.count > 0)` | 任意 | exit2(count 禁进聚合) |
| 23 | `any(events.oscillation.confirmations > events.position_drift.confirmations)` | 任意 | exit2(聚合右侧须字面量/禁跨kind——合法字段仍拒) |
| 24 | `serial.a > serial.b` | 任意 | exit2(var-vs-var 禁止) |
| 25 | `serial.n == 1e3` | 任意 | exit2(不支持指数字面量) |
| 26 | `"a\q"` 出现在任意比较 | 任意 | exit2(裸反斜杠非法转义) |
| 27 | `serial.a==1  &&  serial.b==2`(多空白) | s.a="1",s.b="2" | true(token 间空白忽略) |
