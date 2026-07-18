import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PROTOCOL_PATH = ROOT / "docs" / "aux-eye" / "live-loop.md"
SKILL_PATH = ROOT / ".opencode" / "skills" / "aux-eye-monitor" / "SKILL.md"


def _text(path: Path) -> str:
    assert path.is_file(), "%s is required" % path.relative_to(ROOT)
    return path.read_text(encoding="utf-8")


def test_protocol_has_required_sections_and_exact_predicate_contract():
    protocol = _text(PROTOCOL_PATH)
    draft = _text(ROOT / ".omo" / "drafts" / "aux-eye-live-loop.md")
    canonical = draft[draft.index("BNF:\n") : draft.index("review_required: true", draft.index("BNF:\n"))]

    for heading in (
        "定位", "监控周期", "触发", "时间对齐", "终态机", "fail-closed",
        "相机", "串口", "交接", "Predicate grammar", "Benchmark",
    ):
        assert heading.lower() in protocol.lower()
    assert canonical.strip() in protocol


def test_protocol_defines_bounded_sampling_and_fail_closed_policy():
    protocol = _text(PROTOCOL_PATH)
    for term in (
        "周期性采样巡检", "cycle_deadline_s", "deadline-check", "required_confirmations",
        "stability_windows", "continue", "candidate_success", "success",
        "safe_abort", "needs_human", "串口静默", "重复复位", "可见性丢失",
        "相机身份", "证据冲突", "迭代耗尽", "瞬时", "无检测保证",
    ):
        assert term.lower() in protocol.lower()
    assert "cycle_deadline_s × (required_confirmations + stability_windows)" in protocol


def test_skill_frontmatter_and_modes_are_discoverable():
    skill = _text(SKILL_PATH)
    frontmatter = re.match(r"^---\n(.*?)\n---\n", skill, re.DOTALL)
    assert frontmatter
    assert re.search(r"^name:\s*aux-eye-monitor\s*$", frontmatter.group(1), re.MULTILINE)
    assert re.search(r"^description:\s*\S.+$", frontmatter.group(1), re.MULTILINE)

    calls = (
        'skill(name="aux-eye-monitor", user_message="start --goal goals/inspection.json --camera-name UGREEN --serial-device /dev/tty.usbmodem1101 --baud 115200 --runid inspection-001")',
        'skill(name="aux-eye-monitor", user_message="resume --runid inspection-001")',
        'skill(name="aux-eye-monitor", user_message="status --runid inspection-001")',
        'skill(name="aux-eye-monitor", user_message="abort --runid inspection-001")',
    )
    for call in calls:
        assert call in skill


def test_skill_encodes_exact_cycle_order_and_helper_commands():
    skill = _text(SKILL_PATH)
    ordered = (
        "set-phase --runid $RUNID --phase building",
        "set-phase --runid $RUNID --phase serial_started",
        "set-phase --runid $RUNID --phase flash_started",
        "set-phase --runid $RUNID --phase flashed",
        "set-phase --runid $RUNID --phase captured",
        "set-phase --runid $RUNID --phase evaluated",
        "set-phase --runid $RUNID --phase decided",
    )
    positions = [skill.index(token) for token in ordered]
    assert positions == sorted(positions)

    for token in (
        "tools/build_and_flash.sh --no-flash", "tools/serial_capture.py",
        "set-serial-capture", "set-serial-ready", "[OK][serial] capturing from",
        "tools/camera_capture.py --list", "tools/camera_capture.py --name",
        "tools/aux-eye/verify_aux_eye_temporal.py", "--history",
        "tools/aux-eye/serial_anomaly_scan.py", "tools/aux-eye/aux_eye_goal_decide.py",
        "incr-stability", "reset-stability", "next-action",
        "tools/aux-eye/verify_aux_eye_decision.py", "advance --runid $RUNID --action",
        "deadline-check", "capture-identity",
    ):
        assert token in skill
    goal_command = skill.index("python3 tools/aux-eye/aux_eye_goal_decide.py")
    increment_command = skill.index("python3 tools/aux-eye/aux_eye_run_state.py incr-stability")
    reset_command = skill.index("python3 tools/aux-eye/aux_eye_run_state.py reset-stability")
    decided_command = skill.index("python3 tools/aux-eye/aux_eye_run_state.py set-phase --runid $RUNID --phase decided")
    next_action_command = skill.index("python3 tools/aux-eye/aux_eye_run_state.py next-action")
    assert goal_command < increment_command < next_action_command
    assert goal_command < reset_command < next_action_command
    assert decided_command < next_action_command
    decision_step = skill[skill.index("### 8. Decide, verify, and advance") :]
    cycle_decided_command = decision_step.index(
        "python3 tools/aux-eye/aux_eye_run_state.py set-phase --runid $RUNID --phase decided"
    )
    cycle_next_action_command = decision_step.index(
        "python3 tools/aux-eye/aux_eye_run_state.py next-action"
    )
    decision_verify_command = decision_step.index("python3 tools/aux-eye/verify_aux_eye_decision.py")
    advance_command = decision_step.index("python3 tools/aux-eye/aux_eye_run_state.py advance")
    assert cycle_decided_command < cycle_next_action_command < decision_verify_command < advance_command
    assert re.search(
        r'advance --runid \$RUNID --action "\$ACTION" --terminal-reason "\$TERMINAL_REASON"\n```',
        decision_step,
    )

    protocol = _text(PROTOCOL_PATH)
    evaluated_step = protocol.index("完成后 `set-phase --phase evaluated`")
    decided_step = protocol.index("先设置 `set-phase --phase decided`")
    next_action_step = protocol.index("再执行 `next-action")
    decision_verify_step = protocol.index("tools/aux-eye/verify_aux_eye_decision.py")
    advance_step = protocol.index("执行 `advance --action")
    assert evaluated_step < decided_step < next_action_step < decision_verify_step < advance_step


def test_deadline_gate_and_capture_identity_are_executable_skill_contracts():
    protocol = _text(PROTOCOL_PATH)
    skill = _text(SKILL_PATH)
    cycle = skill[skill.index("## Required Cycle") :]

    for text in (protocol, skill):
        assert "deadline-check --runid" in text
        assert "capture-identity --pid" in text
        assert "cycle_deadline_exceeded" in text
    assert cycle.index("deadline-check --runid") < cycle.index("tools/build_and_flash.sh --no-flash")
    assert cycle.index("deadline-check --runid") < cycle.index("tools/camera_capture.py --list")
    assert cycle.index("deadline-check --runid") < cycle.index("tools/aux-eye/verify_aux_eye_temporal.py")


def test_docs_reject_forbidden_promises_and_placeholders():
    combined = _text(PROTOCOL_PATH) + "\n" + _text(SKILL_PATH)
    forbidden = (
        r"you are a", r"your task is", r"balance.?car", r"wheel.?leg",
        r"near.?realtime", r"实时监控", r"60s/round", r"60 秒/轮",
        r"<skill 路径>", r"输出文件非空", r"output.*non-empty", r"stdout.*非空",
        r"守护进程调", r"python.*drives.*agent",
    )
    for pattern in forbidden:
        assert not re.search(pattern, combined, re.IGNORECASE)
