#!/usr/bin/env python3
"""Agent-executable aux-eye observation verifier + deterministic gates.

Consumes an already-produced observation JSON (this tool NEVER reads pixels,
calls a VLM, or touches the network — it is a pure verifier) and asserts it is
a legal, faithfully-handed-off observation of a specific frame. It is the
machine-checkable gate behind the non-deterministic vision step: it proves the
STRUCTURE and the FRAME-IDENTITY handoff, and never asserts the exact wording
of any description.

This is a *verification tool*, not firmware. It has zero effect on any compiled
binary. It exists so aux-eye observation correctness can be proven with machine
assertions instead of human eyeballs — mirroring tools/verify_goto_def.py's
clean CLI + exit-code + evidence-logging contract.

CLI contract (fixed, no implementation freedom):

    python3 tools/aux-eye/verify_aux_eye.py \
        --frame <frame path> \
        --observation <observation JSON path | -> \
        [--schema schemas/aux-eye/perception.schema.json] \
        [--evidence <path>]

- --frame is REQUIRED: the source frame file whose sha256 the observation
  claims to describe.
- --observation is REQUIRED: path to the observation JSON, or "-" to read it
  from stdin.
- --schema defaults to schemas/aux-eye/perception.schema.json (the single source
  of truth from Todo 4). It is loaded and the observation is validated against
  it with the `jsonschema` package — this ALSO enforces the schema's own
  if/then/else invariant (visible=true <-> failure_reason="none"), so that
  logic is not reimplemented here.

Gates, in order:
  (a) schema-shape : jsonschema.validate(observation, schema).
  (b) frame-identity : sha256(frame) == observation.source_frame_sha256.
  (c) field-logic (only what the schema cannot express as pure shape):
        - visible is a bool (guaranteed by schema, re-checked defensively);
        - IF objects are present, every objects[].confidence is in [0,1], and
          IF a top-level confidence is present it is in [0,1]. objects is
          OPTIONAL per the real schema (Todo 4): len(objects)>=1 is NOT an
          unconditional requirement and is NOT asserted even when visible=true,
          because content fields are vision-dependent and may legitimately be
          empty. This tool therefore does not force any content to exist.
        - visible=false pairs with failure_reason != "none" (also enforced by
          the schema; re-checked so the failure message is explicit).

NO exact-text assertions anywhere: scene.subject and objects[].name are never
compared against expected strings.

Exit 0 if every gate passes; exit 1 with a clear printed list of which gate(s)
failed otherwise. (Exit 2 is reserved for usage errors such as a missing frame
file or unparseable inputs.)
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any

try:
    import jsonschema
except ImportError:
    print(
        "[ERR][verify] jsonschema not installed. "
        "Run: python3 -m pip install jsonschema",
        file=sys.stderr,
    )
    raise SystemExit(2)


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _default_schema_path() -> Path:
    return _repo_root() / "schemas" / "aux-eye" / "perception.schema.json"


def _sha256_of_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fh:  # binary mode: hash the exact bytes on disk
        for chunk in iter(lambda: fh.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _load_observation(observation_arg: str) -> Any:
    """Load the observation JSON from a path or from stdin ('-')."""
    if observation_arg == "-":
        raw = sys.stdin.read()
        return json.loads(raw)
    obs_path = Path(observation_arg)
    return json.loads(obs_path.read_text(encoding="utf-8"))


def _check_confidence_ranges(observation: dict[str, Any], failures: list[str]) -> None:
    """Assert any PRESENT confidence values are in [0, 1].

    objects is optional (real schema, Todo 4) — we do NOT require len>=1. We
    only validate the confidences that actually exist.
    """
    objects = observation.get("objects")
    if isinstance(objects, list):
        for i, obj in enumerate(objects):
            if not isinstance(obj, dict):
                continue
            conf = obj.get("confidence")
            if conf is None:
                continue
            if not isinstance(conf, (int, float)) or isinstance(conf, bool):
                failures.append(
                    "field-logic: objects[%d].confidence=%r is not numeric" % (i, conf)
                )
            elif not (0.0 <= conf <= 1.0):
                failures.append(
                    "field-logic: objects[%d].confidence=%r not in [0,1]" % (i, conf)
                )
    top_conf = observation.get("confidence")
    if top_conf is not None:
        if not isinstance(top_conf, (int, float)) or isinstance(top_conf, bool):
            failures.append(
                "field-logic: top-level confidence=%r is not numeric" % (top_conf,)
            )
        elif not (0.0 <= top_conf <= 1.0):
            failures.append(
                "field-logic: top-level confidence=%r not in [0,1]" % (top_conf,)
            )


def _check_field_logic(observation: dict[str, Any], failures: list[str]) -> None:
    """Field-logic gate (c). Schema already enforces most of this; we re-check
    the parts worth naming explicitly in the failure output."""
    visible = observation.get("visible")
    if not isinstance(visible, bool):
        failures.append("field-logic: visible=%r is not a boolean" % (visible,))

    failure_reason = observation.get("failure_reason")
    if visible is False and failure_reason == "none":
        failures.append(
            "field-logic: visible=false requires failure_reason != 'none' "
            "(got 'none')"
        )
    # NOTE: len(objects)>=1 is intentionally NOT asserted — objects is optional
    # per schemas/aux-eye/perception.schema.json even when visible=true.
    _check_confidence_ranges(observation, failures)


def _write_evidence(evidence: str | None, audit: dict[str, Any]) -> None:
    if not evidence:
        return
    evidence_path = Path(evidence)
    evidence_path.parent.mkdir(parents=True, exist_ok=True)
    with evidence_path.open("a", encoding="utf-8") as fh:
        fh.write("\n===== verify_aux_eye.py audit =====\n")
        fh.write(json.dumps(audit, indent=2, ensure_ascii=False))
        fh.write("\n")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Aux-eye observation verifier + deterministic gates "
        "(schema-shape / frame-identity / field-logic). Asserts no exact text."
    )
    parser.add_argument(
        "--frame",
        required=True,
        help="Path to the source frame file the observation claims to describe.",
    )
    parser.add_argument(
        "--observation",
        required=True,
        help="Path to the observation JSON, or '-' to read it from stdin.",
    )
    parser.add_argument(
        "--schema",
        default=str(_default_schema_path()),
        help="Path to the observation JSON Schema "
        "(default: schemas/aux-eye/perception.schema.json).",
    )
    parser.add_argument(
        "--evidence",
        default=None,
        help="Optional path to append a JSON audit trail for the record.",
    )
    args = parser.parse_args()

    audit: dict[str, Any] = {
        "frame": args.frame,
        "observation": args.observation,
        "schema": args.schema,
    }

    # --- Load inputs (usage errors -> exit 2) ---
    frame_path = Path(args.frame)
    if not frame_path.is_file():
        print("[ERR][verify] frame file not found: %s" % frame_path, file=sys.stderr)
        return 2

    schema_path = Path(args.schema)
    if not schema_path.is_file():
        print("[ERR][verify] schema file not found: %s" % schema_path, file=sys.stderr)
        return 2

    try:
        schema = json.loads(schema_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        print("[ERR][verify] cannot load schema: %r" % exc, file=sys.stderr)
        return 2

    try:
        observation = _load_observation(args.observation)
    except (OSError, ValueError) as exc:
        print("[ERR][verify] cannot load observation JSON: %r" % exc, file=sys.stderr)
        return 2

    if not isinstance(observation, dict):
        print(
            "[ERR][verify] observation must be a JSON object, got %s"
            % type(observation).__name__,
            file=sys.stderr,
        )
        return 2

    failures: list[str] = []

    # --- Gate (a): schema-shape ---
    # jsonschema.validate also enforces the schema's if/then/else invariant
    # (visible<->failure_reason), so we don't reimplement that logic.
    try:
        jsonschema.validate(instance=observation, schema=schema)
        audit["schema_shape"] = "PASS"
    except jsonschema.ValidationError as exc:
        # exc.message is concise; the json path shows exactly which field.
        path = "/".join(str(p) for p in exc.absolute_path) or "<root>"
        failures.append("schema-shape: %s (at %s)" % (exc.message, path))
        audit["schema_shape"] = "FAIL: %s" % exc.message

    # --- Gate (b): frame-identity (sha256) ---
    actual_sha = _sha256_of_file(frame_path)
    claimed_sha = observation.get("source_frame_sha256")
    audit["actual_frame_sha256"] = actual_sha
    audit["claimed_source_frame_sha256"] = claimed_sha
    if claimed_sha != actual_sha:
        failures.append(
            "frame-identity: observation.source_frame_sha256=%r does NOT match "
            "the actual sha256 of the frame (%s). The observation does not "
            "describe this frame." % (claimed_sha, actual_sha)
        )
        audit["frame_identity"] = "FAIL"
    else:
        audit["frame_identity"] = "PASS"

    # --- Gate (c): field-logic ---
    _check_field_logic(observation, failures)
    audit["field_logic"] = "PASS" if not any(
        f.startswith("field-logic") for f in failures
    ) else "FAIL"

    # --- Verdict ---
    if failures:
        print("[ERR][verify] observation FAILED %d check(s):" % len(failures))
        for f in failures:
            print("  - %s" % f)
        audit["verdict"] = "FAIL"
        audit["failures"] = failures
        _write_evidence(args.evidence, audit)
        return 1

    print(
        "[OK][verify] observation PASSED all gates "
        "(schema-shape, frame-identity, field-logic) for frame %s"
        % frame_path.name
    )
    audit["verdict"] = "PASS"
    _write_evidence(args.evidence, audit)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
