import hashlib
import json
from pathlib import Path

import jsonschema
import pytest


_REPO_ROOT = Path(__file__).resolve().parents[2]
_SCHEMAS = _REPO_ROOT / "schemas" / "aux-eye"
_TEMPORAL_FIXTURE = _REPO_ROOT / "tests" / "fixtures" / "aux-eye" / "temporal-still"


def _load_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def test_existing_single_frame_schema_remains_draft_07_and_closed():
    # Given: the existing single-frame contract.
    schema = _load_json(_SCHEMAS / "perception.schema.json")

    # When: its stable boundary fields are inspected.
    actual = (schema["$schema"], schema["additionalProperties"], set(schema["required"]))

    # Then: the pre-existing two-session contract remains unchanged.
    assert actual == (
        "http://json-schema.org/draft-07/schema#",
        False,
        {"source_frame_sha256", "visible", "failure_reason"},
    )


@pytest.mark.parametrize(
    "schema_name",
    [
        "temporal.schema.json",
        "decision.schema.json",
        "goal.schema.json",
    ],
)
def test_new_schema_is_valid_draft_07(schema_name: str):
    # Given: a Todo 1 schema artifact.
    schema = _load_json(_SCHEMAS / schema_name)

    # When / Then: Draft-07 accepts the schema itself.
    jsonschema.Draft7Validator.check_schema(schema)
    assert schema["$schema"] == "http://json-schema.org/draft-07/schema#"
    assert schema["additionalProperties"] is False


def test_temporal_schema_inlines_single_frame_contract_and_rejects_bad_sequences():
    # Given: the temporal and canonical single-frame contracts.
    temporal = _load_json(_SCHEMAS / "temporal.schema.json")
    perception = _load_json(_SCHEMAS / "perception.schema.json")
    observation = temporal["properties"]["frames"]["items"]["properties"]["observation"]

    # When: the inlined observation and closed event shape are inspected.
    inlined = {
        key: observation[key]
        for key in ("type", "additionalProperties", "required", "properties", "if", "then", "else")
    }
    canonical = {
        key: perception[key]
        for key in ("type", "additionalProperties", "required", "properties", "if", "then", "else")
    }
    event = temporal["properties"]["temporal_events"]["items"]

    # Then: the two-session frame contract is exact and event evidence is closed.
    assert inlined == canonical
    assert set(temporal["required"]) == {"frames", "visible"}
    assert temporal["properties"]["frames"]["minItems"] == 1
    assert event["additionalProperties"] is False
    assert "confirmations" in event["required"]
    assert "note" not in event["required"]
    assert set(event["properties"]["status"]["enum"]) == {
        "detected",
        "not_detected",
        "indeterminate",
    }

    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate({"frames": [], "visible": False}, temporal)
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(
            {
                "frames": [{"path": "a", "sha256": "0", "ts": 1}],
                "visible": False,
                "temporal_events": [{"kind": "balance_car"}],
            },
            temporal,
        )


def test_decision_schema_closes_aux_and_serial_evidence():
    # Given: the iteration decision contract.
    schema = _load_json(_SCHEMAS / "decision.schema.json")

    # When: required gates and evidence shapes are inspected.
    required = set(schema["required"])
    aux_event = schema["properties"]["aux_events"]["items"]
    serial_evidence = schema["properties"]["parameter_change_basis"]["properties"][
        "serial_evidence"
    ]["items"]

    # Then: decisions bind goals and keep aux evidence out of parameter authority.
    assert {
        "goal_id",
        "goal_predicate_result",
        "converged",
        "window_ok",
        "firmware_parameters_changed",
        "aux_role",
        "action",
        "reason",
    } <= required
    assert aux_event["additionalProperties"] is False
    assert serial_evidence["additionalProperties"] is False
    assert set(serial_evidence["required"]) == {
        "capture_path",
        "line_number",
        "line_sha256",
    }
    assert "param_change_cited_serial" not in schema["properties"]


def test_goal_schema_has_only_per_run_generic_thresholds():
    # Given: the per-run goal contract.
    schema = _load_json(_SCHEMAS / "goal.schema.json")
    decision = schema["properties"]["decision"]

    # When / Then: goal mode and fail-safe thresholds match the reviewed plan.
    assert set(schema["required"]) == {"goal_description", "decision"}
    assert decision["additionalProperties"] is False
    assert set(decision["properties"]["kind"]["enum"]) == {
        "agent_judgment",
        "predicate",
    }
    assert decision["properties"]["max_iterations"] == {
        "type": "integer",
        "minimum": 1,
        "maximum": 20,
    }
    assert decision["properties"]["stability_windows"]["minimum"] == 1
    assert {
        "serial_silence_s",
        "max_consecutive_resets",
        "visibility_loss_windows",
    } <= set(decision["properties"])


def test_temporal_fixture_is_small_and_hashes_are_computed_from_frames():
    # Given: the deterministic still-sequence fixture.
    metadata = _load_json(_TEMPORAL_FIXTURE / "sequence.gt.json")
    frames = sorted((_TEMPORAL_FIXTURE / "frames").glob("*.jpg"))

    # When: identities are derived from the actual JPEG bytes.
    digests = [hashlib.sha256(frame.read_bytes()).hexdigest() for frame in frames]

    # Then: fixture metadata contains no hashes and stays within repository limits.
    assert metadata == {"frame_count": 3, "visible": True, "monotonic_ts": True}
    assert len(frames) == 3
    assert len(set(digests)) == 1
    assert sum(frame.stat().st_size for frame in frames) <= 2 * 1024 * 1024
    assert "sha256" not in metadata
