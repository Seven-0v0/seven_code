class ExpressionSyntaxError(Exception):
    def __init__(self, source: str, position: int, detail: str) -> None:
        self.source, self.position, self.detail = source, position, detail
        super().__init__(source, position, detail)

    def __str__(self) -> str: return f"{self.detail} at position {self.position}"


class Namespace:
    __slots__ = ("serial", "events")

    def __init__(self, serial: dict, events: dict) -> None:
        self.serial, self.events = serial, events


_TWO_CHARACTER = ("&&", "||", ">=", "<=", "==", "!=")
_ONE_CHARACTER = "!()><"
_COMPARISONS = frozenset((">", ">=", "<", "<=", "==", "!="))
_RELATIONAL = frozenset((">", ">=", "<", "<="))
_EVENT_KINDS = frozenset(("orientation_change", "oscillation", "position_drift", "part_geometry", "out_of_frame", "occluded"))
_AGGREGATE_FIELDS = frozenset(("status", "trend", "start_frame", "end_frame", "confirmations"))
_SCALAR_FIELDS = _AGGREGATE_FIELDS | {"count"}
_STRING_FIELDS = frozenset(("status", "trend"))
_NUMBER_FIELDS = frozenset(("start_frame", "end_frame", "confirmations", "count"))
_NUMBER_COMPARATORS = {">": lambda left, right: left > right, ">=": lambda left, right: left >= right, "<": lambda left, right: left < right, "<=": lambda left, right: left <= right, "==": lambda left, right: left == right, "!=": lambda left, right: left != right}
_STRING_COMPARATORS = {"==": lambda left, right: left == right, "!=": lambda left, right: left != right}
_MISSING = ("missing", None)
_TOKEN_KINDS = frozenset(("symbol", "identifier", "string", "number", "eof"))
_MAX_EXPRESSION_DEPTH = 128


def _syntax(source: str, position: int, detail: str) -> ExpressionSyntaxError: return ExpressionSyntaxError(source, position, detail)


def _is_identifier_start(character: str) -> bool: return character.isascii() and (character.isalpha() or character == "_")


def _is_identifier_part(character: str) -> bool: return character.isascii() and (character.isalnum() or character in "_.")


def _lex_string(source: str, start: int) -> tuple[tuple, int]:
    value = []
    position = start + 1
    while position < len(source):
        character = source[position]
        if character == '"':
            return ("string", "".join(value), start), position + 1
        if character in "\r\n":
            raise _syntax(source, position, "control character in string")
        if character == "\\":
            position += 1
            if position >= len(source) or source[position] not in ('"', "\\"):
                raise _syntax(source, position, "unsupported string escape")
            character = source[position]
        value.append(character)
        position += 1
    raise _syntax(source, start, "unterminated string")


def _lex_number(source: str, start: int) -> tuple[tuple, int]:
    position = start + int(source[start] == "-")
    while position < len(source) and source[position].isascii() and source[position].isdigit():
        position += 1
    if position < len(source) and source[position] == ".":
        position += 1
        decimal_start = position
        while position < len(source) and source[position].isascii() and source[position].isdigit():
            position += 1
        if position == decimal_start:
            raise _syntax(source, position, "decimal point requires digits")
    return ("number", float(source[start:position]), start), position


def _lex(source: str) -> list[tuple]:
    tokens = []
    position = 0
    while position < len(source):
        character = source[position]
        if character.isspace():
            position += 1
            continue
        operator = next((value for value in _TWO_CHARACTER if source.startswith(value, position)), None)
        if operator is not None:
            tokens.append(("symbol", operator, position))
            position += 2
        elif character in _ONE_CHARACTER:
            tokens.append(("symbol", character, position))
            position += 1
        elif character == '"':
            token, position = _lex_string(source, position)
            tokens.append(token)
        elif character.isascii() and character.isdigit() or (
            character == "-"
            and position + 1 < len(source)
            and source[position + 1].isascii()
            and source[position + 1].isdigit()
        ):
            token, position = _lex_number(source, position)
            tokens.append(token)
        elif _is_identifier_start(character):
            start = position
            position += 1
            while position < len(source) and _is_identifier_part(source[position]):
                position += 1
            tokens.append(("identifier", source[start:position], start))
        else:
            raise _syntax(source, position, f"unexpected character {character!r}")
    tokens.append(("eof", "", len(source)))
    return tokens


def _valid_identifier(value: str) -> bool:
    return bool(value) and _is_identifier_start(value[0]) and all(character.isascii() and (character.isalnum() or character == "_") for character in value[1:])


class _Parser:
    """Accept at most 128 grouping, unary, or chained boolean controls."""

    __slots__ = ("source", "tokens", "position", "depth")

    def __init__(self, source: str) -> None:
        self.source = source
        self.tokens = _lex(source)
        self.position, self.depth = 0, 0

    @property
    def current(self) -> tuple: return self.tokens[self.position]

    def take(self, value: str) -> bool:
        if self.current[0] != "symbol" or self.current[1] != value:
            return False
        self.position += 1
        return True

    def expect(self, value: str) -> tuple:
        token = self.current
        matches = token[0] == value if value in _TOKEN_KINDS else token[0] == "symbol" and token[1] == value
        if not matches: raise _syntax(self.source, token[2], f"expected {value!r}")
        self.position += 1
        return token

    def claim_depth(self) -> None:
        self.depth += 1
        if self.depth > _MAX_EXPRESSION_DEPTH: raise _syntax(self.source, self.tokens[self.position - 1][2], "expression exceeds maximum depth")

    def parse(self) -> tuple:
        if self.current[0] == "eof": raise _syntax(self.source, 0, "expression is empty")
        node = self.parse_or()
        self.expect("eof")
        return node

    def parse_or(self) -> tuple:
        node = self.parse_and()
        while self.take("||"):
            self.claim_depth()
            node = ("or", node, self.parse_and())
        return node

    def parse_and(self) -> tuple:
        node = self.parse_not()
        while self.take("&&"):
            self.claim_depth()
            node = ("and", node, self.parse_not())
        return node

    def parse_not(self) -> tuple:
        if self.take("!"):
            self.claim_depth()
            return "not", self.parse_not()
        return self.parse_atom()

    def parse_atom(self) -> tuple:
        if self.take("("):
            self.claim_depth()
            node = self.parse_or()
            self.expect(")")
            return node
        if self.current[0] == "identifier" and self.current[1] in ("any", "all"):
            return self.parse_aggregate()
        return self.parse_comparison()

    def parse_aggregate(self) -> tuple:
        require_all = self.expect("identifier")[1] == "all"
        self.expect("(")
        reference = self.parse_reference(self.expect("identifier"), True)
        operator = self.parse_operator()
        literal = self.parse_literal()
        self.expect(")")
        return "aggregate", require_all, reference, operator, literal

    def parse_comparison(self) -> tuple:
        left = self.parse_operand()
        operator = self.parse_operator()
        right = self.parse_operand()
        if left[0] in ("serial", "event") and right[0] in ("serial", "event"):
            raise _syntax(self.source, self.current[2], "variable comparisons are forbidden")
        return "comparison", left, operator, right

    def parse_operator(self) -> str:
        token = self.expect("symbol")
        if token[1] not in _COMPARISONS: raise _syntax(self.source, token[2], "expected comparison operator")
        return token[1]

    def parse_operand(self) -> tuple:
        if self.current[0] in ("number", "string"):
            return self.parse_literal()
        return self.parse_reference(self.expect("identifier"), False)

    def parse_literal(self) -> tuple:
        token = self.current
        if token[0] not in ("number", "string"): raise _syntax(self.source, token[2], "expected literal")
        self.position += 1
        return token[0], token[1]

    def parse_reference(self, token: tuple, aggregate: bool) -> tuple:
        parts = token[1].split(".")
        if len(parts) == 2 and parts[0] == "serial" and _valid_identifier(parts[1]):
            if aggregate:
                raise _syntax(self.source, token[2], "aggregate requires an event field")
            return "serial", parts[1]
        fields = _AGGREGATE_FIELDS if aggregate else _SCALAR_FIELDS
        if len(parts) == 3 and parts[0] == "events" and parts[1] in _EVENT_KINDS and parts[2] in fields:
            return "event", parts[1], parts[2]
        raise _syntax(self.source, token[2], "reference is not whitelisted")


def _event_value(reference: tuple, event: dict) -> tuple:
    field = reference[2]
    if field not in event:
        return _MISSING
    value = event[field]
    if field in _STRING_FIELDS and type(value) is str:
        return "event-string", value
    if field in _NUMBER_FIELDS and type(value) in (int, float):
        return "event-number", value
    return _MISSING


def _resolve(operand: tuple, namespace: Namespace) -> tuple:
    if operand[0] in ("number", "string"):
        return operand
    if operand[0] == "serial":
        value = namespace.serial.get(operand[1])
        return _MISSING if value is None else ("serial", value)
    events = namespace.events.get(operand[1], [])
    if operand[2] == "count":
        return "event-number", len(events)
    return _MISSING if len(events) != 1 else _event_value(operand, events[0])


def _as_number(resolved: tuple):
    try:
        number = float(resolved[1])
    except (TypeError, ValueError, OverflowError):
        return None
    return number if number == number and abs(number) != float("inf") else None


def _compare(left: tuple, operator: str, right: tuple) -> bool:
    if left[0] == "missing" or right[0] == "missing":
        return False
    if numeric := operator in _RELATIONAL or left[0] == "number" or right[0] == "number":
        if left[0] == "event-string" or right[0] == "event-string": return False
        left_number = _as_number(left)
        right_number = _as_number(right)
        return False if left_number is None or right_number is None else _NUMBER_COMPARATORS[operator](left_number, right_number)
    if left[0] not in ("serial", "string", "event-string") or right[0] not in ("serial", "string", "event-string"):
        return False
    return _STRING_COMPARATORS[operator](left[1], right[1])


def _evaluate_comparison(node: tuple, namespace: Namespace) -> bool:
    left, operator, right = node[1:]
    return _compare(_resolve(left, namespace), operator, _resolve(right, namespace))


def _evaluate_aggregate(node: tuple, namespace: Namespace) -> bool:
    require_all, reference, operator, literal = node[1:]
    right = _resolve(literal, namespace)
    results = (
        _compare(_event_value(reference, event), operator, right)
        for event in namespace.events.get(reference[1], [])
    )
    return all(results) if require_all else any(results)


def _evaluate_node(node: tuple, namespace: Namespace) -> bool:
    if node[0] == "not":
        return not _evaluate_node(node[1], namespace)
    if node[0] == "and":
        return _evaluate_node(node[1], namespace) and _evaluate_node(node[2], namespace)
    if node[0] == "or":
        return _evaluate_node(node[1], namespace) or _evaluate_node(node[2], namespace)
    if node[0] == "aggregate":
        return _evaluate_aggregate(node, namespace)
    return _evaluate_comparison(node, namespace)


def parse(source: str) -> tuple:
    parser = _Parser(source)
    try:
        return parser.parse()
    except RecursionError:
        raise _syntax(source, parser.current[2], "expression exceeds maximum depth") from None


def evaluate(predicate: tuple, namespace: Namespace) -> bool: return _evaluate_node(predicate, namespace)
