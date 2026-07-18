from __future__ import annotations

from pathlib import Path

from live_loop_e2e_cases import (
    evidence_d3_and_two_cycle_success,
    temporal_sessions_and_history,
)
from live_loop_e2e_failsafes import deadline_causality, failsafe_causality, serial_interruption_and_success_bypass


def test_empty_visual_sessions_and_nonfirst_history_are_verified(tmp_path: Path) -> None:
    temporal_sessions_and_history(tmp_path)


def test_run_state_evidence_d3_and_two_window_success_are_chained(tmp_path: Path) -> None:
    evidence_d3_and_two_cycle_success(tmp_path)


def test_failsafes_are_driven_by_run_state_causality(tmp_path: Path) -> None:
    failsafe_causality(tmp_path)


def test_serial_readiness_flash_resume_and_success_bypass_are_fail_closed(tmp_path: Path) -> None:
    serial_interruption_and_success_bypass(tmp_path)


def test_cycle_deadline_is_an_executable_terminal_failsafe(tmp_path: Path) -> None:
    deadline_causality(tmp_path)
