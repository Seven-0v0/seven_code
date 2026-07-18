import importlib.util
import sys
from pathlib import Path

import pytest


_REPO_ROOT = Path(__file__).resolve().parents[2]
_MODULE_PATH = _REPO_ROOT / "tools" / "aux-eye" / "_expr_eval.py"


def _load_module():
    spec = importlib.util.spec_from_file_location("_expr_eval", _MODULE_PATH)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


@pytest.fixture(scope="module")
def expr_eval():
    return _load_module()


@pytest.mark.parametrize(
    ("expression", "serial", "events", "expected"),
    [
        ("serial.uptime > 0", {"uptime": "5"}, {}, True),
        ("serial.uptime > 0", {"uptime": "0"}, {}, False),
        ("serial.missing > 0", {}, {}, False),
        ("!(serial.missing > 0)", {}, {}, True),
        ('serial.mode == "run"', {"mode": "run"}, {}, True),
        ('serial.mode == "run"', {"mode": "idle"}, {}, False),
        ("events.oscillation.count >= 1", {}, {"oscillation": [{}]}, True),
        ("events.oscillation.count >= 1", {}, {"oscillation": []}, False),
        (
            'events.oscillation.status == "detected"',
            {},
            {"oscillation": [{"status": "detected"}]},
            True,
        ),
        (
            'events.oscillation.status == "detected"',
            {},
            {"oscillation": [{"status": "detected"}, {"status": "detected"}]},
            False,
        ),
        (
            "any(events.oscillation.confirmations >= 2)",
            {},
            {"oscillation": [{"confirmations": 1}, {"confirmations": 3}]},
            True,
        ),
        (
            "all(events.oscillation.confirmations >= 2)",
            {},
            {"oscillation": [{"confirmations": 1}, {"confirmations": 3}]},
            False,
        ),
        ('any(events.position_drift.status == "detected")', {}, {}, False),
        ('all(events.position_drift.status == "detected")', {}, {}, True),
        (
            'serial.a > 0 && any(events.oscillation.trend == "increasing")',
            {"a": "1"},
            {"oscillation": [{"trend": "increasing"}]},
            True,
        ),
        ("serial.v == 1", {"v": "1.0"}, {}, True),
        ('serial.v == "1"', {"v": "1.0"}, {}, False),
        ('serial.missing != "x"', {}, {}, False),
        ("serial.t >= -2.5", {"t": "-2.5"}, {}, True),
        ('serial.name == "a\\\"b"', {"name": 'a"b'}, {}, True),
        ("serial.a==1  &&\t serial.b==2", {"a": "1", "b": "2"}, {}, True),
    ],
)
def test_truth_table_boolean_rows(expr_eval, expression, serial, events, expected):
    # Given: one canonical predicate row and its serial/event namespace.
    predicate = expr_eval.parse(expression)
    namespace = expr_eval.Namespace(serial=serial, events=events)

    # When: the parsed predicate is evaluated.
    actual = expr_eval.evaluate(predicate, namespace)

    # Then: it matches the reviewed truth table exactly.
    assert actual is expected


@pytest.mark.parametrize(
    "expression",
    [
        "serial.x >",
        "any(events.oscillation.count > 0)",
        "any(events.oscillation.confirmations > events.position_drift.confirmations)",
        "serial.a > serial.b",
        "serial.n == 1e3",
        'serial.name == "a\\q"',
    ],
)
def test_truth_table_syntax_rows_raise(expr_eval, expression):
    # Given: one canonical expression marked exit 2 in the truth table.
    # When / Then: parsing rejects it before evaluation.
    with pytest.raises(expr_eval.ExpressionSyntaxError):
        expr_eval.parse(expression)


@pytest.mark.parametrize(
    "expression",
    [
        "serial.x = 1",
        "serial.x === 1",
        "serial.x == .5",
        "serial.x == 1.",
        "serial.x == --1",
        'serial.x == "unterminated',
        'serial.x == "line\nbreak"',
        "events.oscillation.confidence > 0",
        "events.unknown.status == \"detected\"",
        "any(serial.x > 0)",
        "all(events.oscillation.status)",
        "serial.x == 1 trailing",
        "",
    ],
)
def test_malformed_lexer_and_non_whitelist_inputs_raise(expr_eval, expression):
    # Given: malformed or non-whitelisted source text.
    # When / Then: the handwritten lexer/parser rejects the boundary input.
    with pytest.raises(expr_eval.ExpressionSyntaxError):
        expr_eval.parse(expression)


def test_two_character_not_equal_token_is_not_split(expr_eval):
    # Given: a two-character operator adjacent to both operands.
    predicate = expr_eval.parse('serial.mode!="idle"')

    # When: it is evaluated as != rather than unary ! followed by an invalid token.
    actual = expr_eval.evaluate(
        predicate,
        expr_eval.Namespace(serial={"mode": "run"}, events={}),
    )

    # Then: the complete operator has precedence in tokenization.
    assert actual is True


def test_parentheses_preserve_boolean_precedence(expr_eval):
    # Given: an expression where grouping changes the default && precedence.
    predicate = expr_eval.parse(
        "(serial.a == 1 || serial.b == 2) && serial.c == 3"
    )

    # When: only the ungrouped left disjunct is true.
    actual = expr_eval.evaluate(
        predicate,
        expr_eval.Namespace(serial={"a": "1", "b": "0", "c": "0"}, events={}),
    )

    # Then: the grouped conjunction remains false.
    assert actual is False


@pytest.mark.parametrize("expression", ["serial.x > 0", "serial.x == 1"])
def test_non_numeric_serial_value_makes_numeric_leaf_false(expr_eval, expression):
    # Given: a numeric comparison whose serial value cannot be coerced to float.
    predicate = expr_eval.parse(expression)

    # When: the comparison is evaluated.
    actual = expr_eval.evaluate(
        predicate,
        expr_eval.Namespace(serial={"x": "not-a-number"}, events={}),
    )

    # Then: type coercion failure is a false leaf, not a syntax/runtime error.
    assert actual is False


@pytest.mark.parametrize(
    ("expression", "serial", "events", "expected"),
    [
        ('serial.x > "1"', {"x": "2"}, {}, True),
        ('serial.x > "abc"', {"x": "2"}, {}, False),
        (
            'events.oscillation.status > "1"',
            {},
            {"oscillation": [{"status": "2"}]},
            False,
        ),
        (
            'any(events.oscillation.status > "1")',
            {},
            {"oscillation": [{"status": "2"}]},
            False,
        ),
    ],
)
def test_relational_syntax_uses_numeric_coercion_or_false_leaf(
    expr_eval, expression, serial, events, expected
):
    # Given: legal relational syntax with a string literal or string event field.
    predicate = expr_eval.parse(expression)

    # When: runtime coercion and closed event kinds are applied.
    actual = expr_eval.evaluate(
        predicate,
        expr_eval.Namespace(serial=serial, events=events),
    )

    # Then: numeric serial data compares, while coercion/type mismatch is false.
    assert actual is expected


@pytest.mark.parametrize(
    ("expression", "events"),
    [
        (
            "events.oscillation.confirmations >= 1",
            {"oscillation": [{"confirmations": "many"}]},
        ),
        (
            "events.oscillation.status == 1",
            {"oscillation": [{"status": "1"}]},
        ),
    ],
)
def test_event_type_mismatch_makes_leaf_false(expr_eval, expression, events):
    # Given: a closed event field with the wrong runtime value kind.
    predicate = expr_eval.parse(expression)

    # When: the field is evaluated.
    actual = expr_eval.evaluate(
        predicate,
        expr_eval.Namespace(serial={}, events=events),
    )

    # Then: the mismatched leaf is false.
    assert actual is False


@pytest.mark.parametrize(
    "expression",
    [
        "serial.x == 1 eof",
        'serial.x == "1" eof',
        'serial.x == 1 "eof"',
        'serial.x == 1 "&&" serial.y == 2',
        '"(" serial.x == 1 ")"',
        'any "(" events.oscillation.status == "detected" ")"',
    ],
)
def test_token_values_cannot_impersonate_grammar_controls(expr_eval, expression):
    # Given: a malformed predicate whose token text resembles a grammar control.
    # When / Then: token kind prevents it from satisfying a symbol or EOF slot.
    with pytest.raises(expr_eval.ExpressionSyntaxError):
        expr_eval.parse(expression)


@pytest.mark.parametrize("value", ["eof", "&&", "(", ")"])
def test_symbol_shaped_string_literals_remain_legal_operands(expr_eval, value):
    # Given: a symbol-shaped string used in a valid operand position.
    predicate = expr_eval.parse(f'serial.x == "{value}"')

    # When: it is evaluated as data rather than parser control.
    actual = expr_eval.evaluate(
        predicate,
        expr_eval.Namespace(serial={"x": value}, events={}),
    )

    # Then: the legal comparison remains true.
    assert actual is True


@pytest.mark.parametrize(
    "expression",
    [
        "(" * 1500 + "serial.x == 1",
        "(" * 1500 + "serial.x == 1" + ")" * 1500,
        "!" * 1500 + "serial.x == 1",
        " && ".join("serial.x == 1" for _ in range(1501)),
        " || ".join("serial.x == 1" for _ in range(1501)),
    ],
)
def test_expression_depth_limit_raises_syntax_error(expr_eval, expression):
    # Given: input that would exceed the bounded parser or AST depth.
    # When / Then: the public parser raises its typed syntax error, not RecursionError.
    with pytest.raises(expr_eval.ExpressionSyntaxError):
        expr_eval.parse(expression)
