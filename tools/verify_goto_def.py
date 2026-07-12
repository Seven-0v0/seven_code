#!/usr/bin/env python3
"""Agent-executable go-to-definition verifier for clangd.

Spawns a `clangd` subprocess (with an explicit --query-driver so it can probe
the ARM cross-compiler exactly like VS Code's settings.json does), drives it
over stdio JSON-RPC through initialize -> initialized -> didOpen -> definition,
and asserts that the definition of the requested symbol resolves to the
expected file + line.

This is a *verification tool*, not firmware. It has zero effect on any compiled
binary. It exists so index/go-to-definition correctness can be proven with
machine assertions instead of human eyeballs.

CLI contract (fixed, no implementation freedom):

    python3 tools/verify_goto_def.py --compiler <CC> \
        [--file apps/blinky_f103/src/main.c] \
        [--line 255] [--symbol UART1_Init] \
        [--evidence <path>]

- --compiler is REQUIRED and passed as-is to `clangd --query-driver=<compiler>`.
  The script never reads CMakeCache itself; the caller injects the path so this
  stays testable and decoupled from build layout.
- Default target: the `UART1_Init` call site in main.c. LSP coordinates are
  0-based: source line 256 == LSP line 255. The character is auto-located at
  the start of the --symbol identifier on that line (falls back to a fixed
  column if the symbol is not found textually).
- On the definition response the script asserts the returned uri ends with the
  --file suffix AND range.start.line == --def-line (default 85, i.e. source
  :86). Exit 0 on success, non-zero on any failure.
- The full request, raw response, and exit code are appended to the evidence
  file for the audit trail.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any


def _repo_root() -> Path:
    # tools/verify_goto_def.py -> repo root is one level up.
    return Path(__file__).resolve().parent.parent


class LspClient:
    """Minimal stdio JSON-RPC client for a language server subprocess."""

    def __init__(self, proc: subprocess.Popen[bytes]) -> None:
        self._proc = proc
        self._next_id = 0
        self._stderr_chunks: list[str] = []
        self._stderr_thread = threading.Thread(target=self._drain_stderr, daemon=True)
        self._stderr_thread.start()

    def _drain_stderr(self) -> None:
        assert self._proc.stderr is not None
        for raw in self._proc.stderr:
            try:
                self._stderr_chunks.append(raw.decode("utf-8", "replace"))
            except Exception:
                pass

    @property
    def stderr_text(self) -> str:
        return "".join(self._stderr_chunks)

    def _write_message(self, payload: dict[str, Any]) -> None:
        assert self._proc.stdin is not None
        body = json.dumps(payload).encode("utf-8")
        header = f"Content-Length: {len(body)}\r\n\r\n".encode("ascii")
        self._proc.stdin.write(header)
        self._proc.stdin.write(body)
        self._proc.stdin.flush()

    def notify(self, method: str, params: dict[str, Any]) -> None:
        self._write_message({"jsonrpc": "2.0", "method": method, "params": params})

    def request(self, method: str, params: dict[str, Any]) -> dict[str, Any]:
        self._next_id += 1
        req_id = self._next_id
        self._write_message(
            {"jsonrpc": "2.0", "id": req_id, "method": method, "params": params}
        )
        return self._read_until_id(req_id)

    def _read_headers(self) -> dict[str, str]:
        assert self._proc.stdout is not None
        headers: dict[str, str] = {}
        while True:
            line = self._proc.stdout.readline()
            if not line:
                raise EOFError("clangd closed stdout while reading headers")
            text = line.decode("ascii", "replace").strip()
            if text == "":
                return headers
            if ":" in text:
                key, _, value = text.partition(":")
                headers[key.strip().lower()] = value.strip()

    def _read_message(self, timeout_s: float) -> dict[str, Any]:
        assert self._proc.stdout is not None
        deadline = time.monotonic() + timeout_s
        headers = self._read_headers()
        length = int(headers.get("content-length", "0"))
        body = b""
        while len(body) < length:
            if time.monotonic() > deadline:
                raise TimeoutError("timed out reading LSP message body")
            chunk = self._proc.stdout.read(length - len(body))
            if not chunk:
                raise EOFError("clangd closed stdout while reading body")
            body += chunk
        return json.loads(body.decode("utf-8"))

    def _read_until_id(self, req_id: int, timeout_s: float = 60.0) -> dict[str, Any]:
        deadline = time.monotonic() + timeout_s
        while True:
            if time.monotonic() > deadline:
                raise TimeoutError(f"timed out waiting for response id={req_id}")
            msg = self._read_message(timeout_s=deadline - time.monotonic())
            if msg.get("id") == req_id and ("result" in msg or "error" in msg):
                return msg
            # ignore notifications / server->client requests / other responses

    def shutdown(self) -> None:
        try:
            self.request("shutdown", {})
            self.notify("exit", {})
        except Exception:
            pass
        try:
            self._proc.wait(timeout=5)
        except Exception:
            self._proc.kill()


def _path_to_uri(path: Path) -> str:
    return path.resolve().as_uri()


def _locate_symbol_column(line_text: str, symbol: str, fallback: int) -> int:
    idx = line_text.find(symbol)
    if idx < 0:
        return fallback
    # Point inside the identifier (its start column is fine for clangd).
    return idx


def _extract_locations(result: Any) -> list[dict[str, Any]]:
    """Normalize Location | Location[] | LocationLink[] into a list of dicts."""
    if result is None:
        return []
    items = result if isinstance(result, list) else [result]
    locations: list[dict[str, Any]] = []
    for item in items:
        if not isinstance(item, dict):
            continue
        if "uri" in item and "range" in item:  # Location
            locations.append({"uri": item["uri"], "range": item["range"]})
        elif "targetUri" in item:  # LocationLink
            rng = item.get("targetSelectionRange") or item.get("targetRange")
            locations.append({"uri": item["targetUri"], "range": rng})
    return locations


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--compiler",
        required=True,
        help="Cross-compiler path, passed verbatim to clangd --query-driver=",
    )
    parser.add_argument(
        "--file",
        default="apps/blinky_f103/src/main.c",
        help="Source file (repo-relative) to open and query.",
    )
    parser.add_argument(
        "--line",
        type=int,
        default=255,
        help="LSP 0-based line of the call site (source line 256 -> 255).",
    )
    parser.add_argument(
        "--character",
        type=int,
        default=None,
        help="LSP 0-based column; if omitted, auto-located at the symbol.",
    )
    parser.add_argument("--symbol", default="UART1_Init", help="Identifier to resolve.")
    parser.add_argument(
        "--def-line",
        type=int,
        default=85,
        help="Expected LSP 0-based line of the definition (source :86 -> 85).",
    )
    parser.add_argument(
        "--clangd", default="clangd", help="clangd executable to use."
    )
    parser.add_argument(
        "--evidence",
        default=None,
        help="Optional path to append the request/response/exit-code audit trail.",
    )
    args = parser.parse_args()

    repo_root = _repo_root()
    src_path = (repo_root / args.file).resolve()
    if not src_path.is_file():
        print(f"FAIL: source file not found: {src_path}", file=sys.stderr)
        return 2

    file_text = src_path.read_text(encoding="utf-8")
    lines = file_text.splitlines()
    if args.line >= len(lines):
        print(
            f"FAIL: --line {args.line} out of range (file has {len(lines)} lines)",
            file=sys.stderr,
        )
        return 2

    character = (
        args.character
        if args.character is not None
        else _locate_symbol_column(lines[args.line], args.symbol, fallback=4)
    )

    uri = _path_to_uri(src_path)
    audit: dict[str, Any] = {
        "compiler": args.compiler,
        "clangd": args.clangd,
        "file": str(src_path),
        "uri": uri,
        "position": {"line": args.line, "character": character},
        "symbol": args.symbol,
        "expected_uri_suffix": args.file,
        "expected_def_line": args.def_line,
    }

    proc = subprocess.Popen(
        [
            args.clangd,
            f"--query-driver={args.compiler}",
            "--background-index=false",
        ],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=str(repo_root),
    )
    client = LspClient(proc)

    exit_code = 1
    try:
        init_result = client.request(
            "initialize",
            {
                "processId": os.getpid(),
                "rootUri": _path_to_uri(repo_root),
                "capabilities": {
                    "textDocument": {
                        "definition": {"linkSupport": True},
                        "synchronization": {"didSave": False},
                    }
                },
            },
        )
        audit["initialize_result_keys"] = sorted(
            (init_result.get("result") or {}).keys()
        )
        client.notify("initialized", {})

        client.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": uri,
                    "languageId": "c",
                    "version": 1,
                    "text": file_text,
                }
            },
        )

        # Give clangd a moment to parse the TU before querying.
        time.sleep(1.5)

        def_response = client.request(
            "textDocument/definition",
            {
                "textDocument": {"uri": uri},
                "position": {"line": args.line, "character": character},
            },
        )
        audit["definition_request"] = {
            "textDocument": {"uri": uri},
            "position": {"line": args.line, "character": character},
        }
        audit["definition_response"] = def_response

        if "error" in def_response:
            audit["assertion"] = f"clangd error: {def_response['error']}"
            print(f"FAIL: clangd returned error: {def_response['error']}", file=sys.stderr)
            exit_code = 3
        else:
            locations = _extract_locations(def_response.get("result"))
            audit["normalized_locations"] = locations
            matched = False
            for loc in locations:
                loc_uri = str(loc.get("uri", ""))
                rng = loc.get("range") or {}
                start_line = (rng.get("start") or {}).get("line")
                if loc_uri.endswith(args.file) and start_line == args.def_line:
                    matched = True
                    audit["matched_location"] = loc
                    break
            if matched:
                audit["assertion"] = "PASS"
                print(
                    f"PASS: {args.symbol} at {args.file}:{args.line + 1} "
                    f"resolves to definition at line {args.def_line + 1} "
                    f"(LSP line {args.def_line})."
                )
                exit_code = 0
            else:
                audit["assertion"] = "FAIL: no location matched expected uri+line"
                print(
                    f"FAIL: expected a definition with uri endswith '{args.file}' "
                    f"and range.start.line == {args.def_line}; got {locations}",
                    file=sys.stderr,
                )
                exit_code = 4
    except (EOFError, TimeoutError, ValueError, OSError) as exc:
        audit["exception"] = repr(exc)
        audit["clangd_stderr_tail"] = client.stderr_text[-4000:]
        print(f"FAIL: {exc!r}", file=sys.stderr)
        exit_code = 5
    finally:
        client.shutdown()
        audit["exit_code"] = exit_code
        audit["clangd_stderr_tail"] = client.stderr_text[-4000:]
        if args.evidence:
            evidence_path = Path(args.evidence)
            evidence_path.parent.mkdir(parents=True, exist_ok=True)
            with evidence_path.open("a", encoding="utf-8") as fh:
                fh.write("\n===== verify_goto_def.py audit =====\n")
                fh.write(json.dumps(audit, indent=2, ensure_ascii=False))
                fh.write("\n")

    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
