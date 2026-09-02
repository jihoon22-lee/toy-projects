"""Command-line behavior tests without depending on installed host packages."""

from __future__ import annotations

import io
import json
import sys
from contextlib import redirect_stderr, redirect_stdout
from datetime import datetime, timezone
from pathlib import Path
from unittest.mock import patch

import pytest

from envlens import __main__ as cli
from envlens.probe import ProbeError
from envlens.snapshot import SnapshotError


def test_cli_snapshot_writes_canonical_json_to_stdout() -> None:
    snapshot = {"schema_version": "envlens.snapshot/v1", "ok": True}
    out = io.StringIO()
    with (
        patch.object(
            cli,
            "resolve_interpreter",
            return_value=(Path("/resolved/python"), "requested"),
        ),
        patch.object(cli, "collect_snapshot", return_value=snapshot) as collect,
        patch.object(cli, "dumps_snapshot", return_value='{"ok":true}\n') as dumps,
        redirect_stdout(out),
    ):
        result = cli.main(
            [
                "snapshot",
                "--interpreter",
                "/tmp/requested/python",
                "--timeout-seconds",
                "3",
                "--captured-at",
                "2024-01-02T03:04:05+09:00",
            ]
        )

    assert result == 0
    assert out.getvalue() == '{"ok":true}\n'
    collect.assert_called_once_with(
        Path("/tmp/requested/python"),
        timeout_seconds=3,
        captured_at=datetime(2024, 1, 1, 18, 4, 5, tzinfo=timezone.utc),
    )
    dumps.assert_called_once_with(snapshot, pretty=False)


def test_cli_snapshot_writes_file_and_honors_pretty(tmp_path: Path) -> None:
    output = tmp_path / "snapshot.json"
    snapshot = {"schema_version": "envlens.snapshot/v1", "value": "ok"}
    with (
        patch.object(
            cli,
            "resolve_interpreter",
            return_value=(Path("/resolved/python"), "requested"),
        ),
        patch.object(cli, "collect_snapshot", return_value=snapshot),
    ):
        result = cli.main(
            [
                "snapshot",
                "--interpreter",
                "/tmp/requested/python",
                "--output",
                str(output),
                "--pretty",
            ]
        )

    assert result == 0
    parsed = json.loads(output.read_text(encoding="utf-8"))
    assert parsed == snapshot
    assert output.read_text(encoding="utf-8").endswith("\n")


def test_cli_version_exits_successfully() -> None:
    out = io.StringIO()
    with redirect_stdout(out), pytest.raises(SystemExit) as caught:
        cli.main(["--version"])

    assert caught.value.code == 0
    assert out.getvalue() == "envlens 0.1.0\n"


def test_cli_reports_probe_errors_as_user_facing_exit_two() -> None:
    err = io.StringIO()
    with (
        patch.object(
            cli,
            "resolve_interpreter",
            side_effect=ProbeError("invalid-interpreter", "bad executable"),
        ),
        redirect_stderr(err),
        pytest.raises(SystemExit) as caught,
    ):
        cli.main(["snapshot"])

    assert caught.value.code == 2
    assert err.getvalue() == "envlens: invalid-interpreter: bad executable\n"


def test_cli_rejects_output_that_is_the_selected_interpreter() -> None:
    err = io.StringIO()
    with (
        patch.object(
            cli,
            "resolve_interpreter",
            return_value=(Path(sys.executable), "requested"),
        ),
        patch.object(cli, "collect_snapshot") as collect,
        redirect_stderr(err),
        pytest.raises(SystemExit) as caught,
    ):
        cli.main(["snapshot", "--output", sys.executable])

    assert caught.value.code == 2
    assert "output must not replace the selected interpreter" in err.getvalue()
    collect.assert_not_called()


def test_cli_rejects_timestamp_without_utc_offset() -> None:
    err = io.StringIO()
    with redirect_stderr(err), pytest.raises(SystemExit) as caught:
        cli.main(["snapshot", "--captured-at", "2024-01-02T03:04:05"])

    assert caught.value.code == 2
    assert "timestamp must include a UTC offset" in err.getvalue()


def test_cli_reports_snapshot_errors_as_user_facing_exit_two() -> None:
    err = io.StringIO()
    with (
        patch.object(
            cli,
            "resolve_interpreter",
            return_value=(Path("/resolved/python"), "requested"),
        ),
        patch.object(
            cli,
            "collect_snapshot",
            side_effect=SnapshotError("probe-timeout", "timed out"),
        ),
        redirect_stderr(err),
        pytest.raises(SystemExit) as caught,
    ):
        cli.main(["snapshot"])

    assert caught.value.code == 2
    assert err.getvalue() == "envlens: probe-timeout: timed out\n"
