---
name: aux-eye-monitor
description: Use when starting, resuming, inspecting, or aborting an Aux-Eye periodic sampled inspection run with a goal file, camera, and serial device.
---

# Aux-Eye Monitor

Follow [the protocol](../../../docs/aux-eye/live-loop.md) as the authority. The loop is agent-driven. Run the listed deterministic helpers, read each exit code, and stop closed on any unmet gate.

## Harness Modes

Use these discoverable calls:

```text
skill(name="aux-eye-monitor", user_message="start --goal goals/inspection.json --camera-name UGREEN --serial-device /dev/tty.usbmodem1101 --baud 115200 --runid inspection-001")
skill(name="aux-eye-monitor", user_message="resume --runid inspection-001")
skill(name="aux-eye-monitor", user_message="status --runid inspection-001")
skill(name="aux-eye-monitor", user_message="abort --runid inspection-001")
```

### start

Require `--goal`, `--camera-name`, `--serial-device`, `--baud`, and `--runid`. Validate the goal against `schemas/aux-eye/goal.schema.json`. Apply protocol defaults to omitted decision settings, including `max_consecutive_resets=2` with the strict `consecutive_boot_count > max_consecutive_resets` gate. Preflight the camera and initialize state:

```bash
python3 tools/camera_capture.py --list
python3 tools/camera_capture.py --name "$CAMERA_NAME" --frames 1 --timeout 15 --outdir ".omo/evidence/aux-eye-monitor/$RUNID/preflight"
GOAL_ID="$(python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' "$GOAL")"
python3 tools/aux-eye/aux_eye_run_state.py init --runid "$RUNID" --goal-id "$GOAL_ID" --goal-path "$GOAL" --camera-index "$CAMERA_INDEX" --camera-name "$CAMERA_FULL_NAME" --serial-device "$SERIAL_DEVICE" --serial-baud "$BAUD" --max-iterations "$MAX_ITERATIONS"
```

Read `CAMERA_INDEX` and `CAMERA_FULL_NAME` from the preflight listing and NDJSON. Don't infer either value. Enter the cycle below.

### resume

Read the stored goal path and run the safety check:

```bash
python3 tools/aux-eye/aux_eye_run_state.py get --runid "$RUNID"
python3 tools/aux-eye/aux_eye_run_state.py resume-check --runid "$RUNID" --goal-path "$GOAL"
```

If `resume-check` reports `flash_interrupted`, stop at `needs_human`. If it clears a dead or identity-mismatched capture outside that interrupted flash state, restart capture at `serial_started`. Continue from the persisted `cycle_phase`. Never repeat a flash when its completion is uncertain.

### status

```bash
python3 tools/aux-eye/aux_eye_run_state.py get --runid "$RUNID"
python3 tools/aux-eye/aux_eye_run_state.py is-terminal --runid "$RUNID"
```

Report the JSON and terminal exit status without changing state. A missing runid is a normal status error, not a reason to create a run.

### abort

```bash
python3 tools/aux-eye/aux_eye_run_state.py advance --runid "$RUNID" --action safe_abort --terminal-reason user_abort
```

## Required Cycle

`set-phase --phase building` persists the authoritative monotonic start for the 180 second `cycle_deadline_s`. Run this gate before and after every bounded or irreversible step:

```bash
python3 tools/aux-eye/aux_eye_run_state.py deadline-check --runid "$RUNID"
```

On expiry, `deadline-check` atomically clears only an identity-verified serial capture, records `needs_human` with `cycle_deadline_exceeded`, and locks all later mutations. Do not call `advance` or another cleanup command after that terminal result.

### 1. Build

```bash
python3 tools/aux-eye/aux_eye_run_state.py set-phase --runid $RUNID --phase building
python3 tools/aux-eye/aux_eye_run_state.py deadline-check --runid "$RUNID"
bash tools/build_and_flash.sh --no-flash
python3 tools/aux-eye/aux_eye_run_state.py deadline-check --runid "$RUNID"
```

Only make changes authorized by the surrounding development workflow. Build failure clears resources and enters `safe_abort`.

### 2. Start serial before flash

```bash
python3 tools/aux-eye/aux_eye_run_state.py set-phase --runid $RUNID --phase serial_started
python3 tools/serial_capture.py --device "$SERIAL_DEVICE" --baud "$BAUD" --timeout 180 >"$SERIAL_OUT" 2>"$SERIAL_ERR" &
SERIAL_PID=$!
SERIAL_IDENTITY="$(python3 tools/aux-eye/aux_eye_run_state.py capture-identity --pid "$SERIAL_PID")"
SERIAL_IDENTITY="$(python3 -c 'import json,sys; print(json.loads(sys.stdin.read())["identity"])' <<<"$SERIAL_IDENTITY")"
python3 tools/aux-eye/aux_eye_run_state.py set-serial-capture --runid "$RUNID" --pid "$SERIAL_PID" --identity "$SERIAL_IDENTITY" --stdout "$SERIAL_OUT" --stderr "$SERIAL_ERR"
python3 tools/aux-eye/aux_eye_run_state.py set-serial-ready --runid "$RUNID"
python3 tools/aux-eye/aux_eye_run_state.py deadline-check --runid "$RUNID"
```

Poll `set-serial-ready` only until the bounded startup timeout. Readiness requires a live PID and the stderr marker `[OK][serial] capturing from`. Serial payload bytes are unrelated to readiness. Keep this process alive across flash and reset.

### 3. Flash

```bash
python3 tools/aux-eye/aux_eye_run_state.py set-phase --runid $RUNID --phase flash_started
python3 tools/aux-eye/aux_eye_run_state.py deadline-check --runid "$RUNID"
bash tools/build_and_flash.sh
python3 tools/aux-eye/aux_eye_run_state.py deadline-check --runid "$RUNID"
python3 tools/aux-eye/aux_eye_run_state.py set-flash-done --runid "$RUNID"
python3 tools/aux-eye/aux_eye_run_state.py set-phase --runid $RUNID --phase flashed
```

Record `flash_started` before invoking the flash command. If flash fails or the session stops before `set-flash-done`, resume must enter `needs_human`.

### 4. Verify camera mapping and capture

```bash
python3 tools/camera_capture.py --list
python3 tools/aux-eye/aux_eye_run_state.py deadline-check --runid "$RUNID"
python3 tools/camera_capture.py --name "$CAMERA_NAME" --frames 3 --interval 1 --timeout 15 --outdir "$CYCLE_DIR/frames" >"$CYCLE_DIR/camera.ndjson" 2>"$CYCLE_DIR/camera.err"
python3 tools/aux-eye/aux_eye_run_state.py deadline-check --runid "$RUNID"
python3 tools/aux-eye/aux_eye_run_state.py set-phase --runid $RUNID --phase captured
```

Before capture, verify the stored index still maps to the stored full name. After capture, verify every NDJSON `index` matches that mapping. Drift, ambiguity, no usable frame, or mismatch enters `needs_human` with `camera_identity` or `visibility_loss`.

### 5. Observe and verify temporal history

Read every frame through the available image surface. Write `$CYCLE_DIR/observation.json` using `schemas/aux-eye/temporal.schema.json`. For cycle 1:

```bash
python3 tools/aux-eye/verify_aux_eye_temporal.py --sequence-dir "$CYCLE_DIR" --observation "$CYCLE_DIR/observation.json" --evidence "$CYCLE_DIR/temporal-audit.jsonl"
python3 tools/aux-eye/aux_eye_run_state.py deadline-check --runid "$RUNID"
```

For every non-first cycle, `--history` is mandatory:

```bash
python3 tools/aux-eye/verify_aux_eye_temporal.py --sequence-dir "$CYCLE_DIR" --observation "$CYCLE_DIR/observation.json" --history "$HISTORY_DIR" --evidence "$CYCLE_DIR/temporal-audit.jsonl"
python3 tools/aux-eye/aux_eye_run_state.py deadline-check --runid "$RUNID"
```

Don't continue after a failed schema, identity, ordering, roll-up, frame-index, or confirmations gate.

### 6. Evaluate serial and goal

```bash
python3 tools/aux-eye/serial_anomaly_scan.py --input "$SERIAL_OUT" --predicate "$SERIAL_PREDICATE" --evidence "$CYCLE_DIR/serial-audit.jsonl"
python3 tools/aux-eye/aux_eye_run_state.py deadline-check --runid "$RUNID"
python3 tools/aux-eye/aux_eye_goal_decide.py --goal "$GOAL" --temporal-observation "$CYCLE_DIR/observation.json" --serial "$SERIAL_OUT" --runid "$RUNID" --evidence "$CYCLE_DIR/goal-audit.jsonl" >"$CYCLE_DIR/goal-result.json"
python3 tools/aux-eye/aux_eye_run_state.py deadline-check --runid "$RUNID"
python3 tools/aux-eye/aux_eye_run_state.py set-phase --runid $RUNID --phase evaluated
```

For an `agent_judgment` goal, add `--agent-window-ok true` or `--agent-window-ok false` to the goal command and cite concrete frame and serial evidence in the decision record.

### 7. Update stability from the current window

Read `goal_predicate_result` for predicate goals or `agent_window_ok` for agent judgment. This is `WINDOW_OK`. Run exactly one branch after `aux_eye_goal_decide.py` and before `next-action`:

```bash
python3 tools/aux-eye/aux_eye_run_state.py incr-stability --runid "$RUNID"
python3 tools/aux-eye/aux_eye_run_state.py reset-stability --runid "$RUNID"
```

Run `incr-stability` only when the current window is true. Run `reset-stability` for false or indeterminate. Don't choose the branch from `converged`.

### 8. Decide, verify, and advance

```bash
python3 tools/aux-eye/aux_eye_run_state.py set-phase --runid $RUNID --phase decided
python3 tools/aux-eye/aux_eye_run_state.py next-action --runid "$RUNID" --goal "$GOAL" --converged "$CONVERGED" --window-ok "$WINDOW_OK" --camera-index-now "$CAMERA_INDEX" --camera-name-now "$CAMERA_FULL_NAME" >"$CYCLE_DIR/next-action.json"
python3 tools/aux-eye/verify_aux_eye_decision.py --decision "$CYCLE_DIR/decision.json" --evidence "$CYCLE_DIR/decision-audit.jsonl"
python3 tools/aux-eye/aux_eye_run_state.py deadline-check --runid "$RUNID"
python3 tools/aux-eye/aux_eye_run_state.py advance --runid $RUNID --action "$ACTION" --terminal-reason "$TERMINAL_REASON"
```

After `set-phase --phase decided`, construct `decision.json` from the verified temporal and serial evidence plus the persisted pending action from `next-action`. Normal `success`, `candidate_success`, and `continue` are selected only after `decided`; fail-safe `needs_human` remains available earlier when a safety gate trips. `success` requires `converged=true`. `candidate_success` requires `window_ok=true` and starts another cycle. `continue` starts another cycle. `safe_abort` and `needs_human` stop. `advance` atomically reaps only the identity-verified serial capture for every accepted action, so it is the normal-cycle cleanup owner. Evidence conflict, fail-safe thresholds, camera drift, iteration exhaustion, deadline expiry, or interrupted flash must produce `needs_human`.
