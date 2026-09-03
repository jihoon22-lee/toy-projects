"""Process-result classification helpers for runtime checks."""

from __future__ import annotations

import signal
from collections.abc import Callable
from typing import Any


def bounded_text(data: bytes, max_chars: int) -> str:
    text = data.decode("utf-8", errors="replace")
    if len(text) > max_chars:
        return text[:max_chars] + "…"
    return text


def failed_process_status(return_code: int, stderr: str, name: str) -> dict[str, Any]:
    if return_code < 0:
        number = -return_code
        try:
            signal_name = signal.Signals(number).name
        except ValueError:
            signal_name = f"SIG{number}"
        return {"status": "signal", "signal": number, "signal_name": signal_name}
    markers = (
        ("ENVLENS_MISSING_IMPORT=", "missing-import"),
        ("ENVLENS_IMPORT_ERROR=", "import-error"),
        ("ENVLENS_ENTRY_POINT_ERROR=", "entry-point-error"),
    )
    for marker, status in markers:
        if marker not in stderr:
            continue
        detail: dict[str, Any] = {"status": status}
        if status == "missing-import":
            missing = stderr.split(marker, 1)[1].splitlines()[0].strip()
            detail["missing_import"] = missing or name
        return detail
    return {"status": "failed"}


def classify_process(
    *,
    stdout: bytes,
    stderr: bytes,
    return_code: int,
    timeout_seconds: int,
    kind: str,
    name: str,
    max_output_chars: int,
    text_renderer: Callable[[bytes], str] | None = None,
    status_renderer: Callable[[int, str, str], dict[str, Any]] | None = None,
) -> dict[str, Any]:
    render_text = (
        (lambda value: bounded_text(value, max_output_chars))
        if text_renderer is None
        else text_renderer
    )
    classify_status = failed_process_status if status_renderer is None else status_renderer
    stderr_text = render_text(stderr).strip()
    passed = return_code == 0
    result: dict[str, Any] = {
        "kind": kind,
        "name": name,
        "status": "passed" if passed else "failed",
        "return_code": return_code,
    }
    if stdout:
        result["stdout"] = render_text(stdout)
    if stderr_text:
        result["stderr"] = stderr_text
    if not passed:
        result.update(classify_status(return_code, stderr_text, name))
    result["reason"] = (
        f"{kind} completed successfully" if passed else f"{kind} process exited {return_code}"
    )
    result["timeout_seconds"] = timeout_seconds
    return result
