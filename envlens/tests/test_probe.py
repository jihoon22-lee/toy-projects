"""Protocol and process-boundary tests for envlens.probe."""

from __future__ import annotations

import io
import os
import signal
import stat
import subprocess
import sys
import time
from contextlib import suppress
from pathlib import Path
from unittest.mock import patch

import pytest

from envlens import probe


def _make_executable(tmp_path: Path, output: bytes = b"", *, source: str | None = None) -> Path:
    """Create an executable that emits fixed bytes and ignores its argv."""

    executable = tmp_path / "interpreter with spaces"
    if source is None:
        source = "import sys; sys.stdout.buffer.write(" + repr(output) + ")"
    executable.write_text("#!" + sys.executable + "\n" + source + "\n", encoding="utf-8")
    executable.chmod(executable.stat().st_mode | stat.S_IXUSR)
    return executable


def _probe_json(**extra: object) -> bytes:
    fields = {"schema_version": "envlens.probe/v1"}
    fields.update(extra)
    import json

    return json.dumps(fields, separators=(",", ":")).encode("utf-8")


def test_collect_probe_passes_fixed_argv_to_bounded_runner(tmp_path: Path) -> None:
    executable = _make_executable(tmp_path)
    calls: list[tuple[list[str], int]] = []

    def fake_run(command: list[str], timeout_seconds: int) -> tuple[bytes, bytes, int]:
        calls.append((command, timeout_seconds))
        return _probe_json(), b"", 0

    with patch.object(probe, "_run_bounded", side_effect=fake_run):
        payload, resolved, requested = probe.collect_probe(executable, timeout_seconds=7)

    assert payload == {"schema_version": "envlens.probe/v1"}
    assert resolved == executable.resolve()
    assert requested == str(executable)
    assert calls == [([str(executable), "-c", probe.PROBE_SCRIPT], 7)]


def test_collect_probe_runs_the_requested_path_through_a_symlink(tmp_path: Path) -> None:
    executable = _make_executable(tmp_path)
    link = tmp_path / "linked-interpreter"
    link.symlink_to(executable)
    calls: list[list[str]] = []

    def fake_run(command: list[str], timeout_seconds: int) -> tuple[bytes, bytes, int]:
        calls.append(command)
        return _probe_json(), b"", 0

    with patch.object(probe, "_run_bounded", side_effect=fake_run):
        _payload, resolved, requested = probe.collect_probe(link, timeout_seconds=7)

    # argv[0] keeps the requested name so venv/conda markers next to the
    # symlink stay visible to the interpreter; resolved is metadata only.
    assert resolved == executable.resolve()
    assert requested == str(link)
    assert calls == [[str(link), "-c", probe.PROBE_SCRIPT]]


def test_run_bounded_uses_argv_and_disables_shell() -> None:
    class FakeStream:
        def read(self, _size: int) -> bytes:
            return b""

    class FakeProcess:
        pid = 12345

        def __init__(self) -> None:
            self.stdout = FakeStream()
            self.stderr = FakeStream()

        def wait(self, timeout: int | None = None) -> int:
            assert timeout == 3
            return 0

    calls: list[tuple[list[str], dict[str, object]]] = []

    def fake_popen(command: list[str], **kwargs: object) -> FakeProcess:
        calls.append((command, kwargs))
        return FakeProcess()

    with patch.object(probe.subprocess, "Popen", side_effect=fake_popen):
        stdout, stderr, return_code = probe._run_bounded(
            ["path with spaces; do-not-run", "-c", "print('safe')"], timeout_seconds=3
        )

    assert (stdout, stderr, return_code) == (b"", b"", 0)
    assert len(calls) == 1
    command, kwargs = calls[0]
    assert command == ["path with spaces; do-not-run", "-c", "print('safe')"]
    assert kwargs["shell"] is False
    assert kwargs["stdin"] is subprocess.DEVNULL
    assert kwargs["stdout"] is subprocess.PIPE
    assert kwargs["stderr"] is subprocess.PIPE
    assert kwargs["start_new_session"] is (os.name == "posix")


def test_drain_retains_only_one_byte_beyond_probe_limit() -> None:
    chunks: list[bytes] = []
    probe._drain(
        io.BytesIO(b"x" * (probe.MAX_PROBE_BYTES + 4096)),
        probe.MAX_PROBE_BYTES,
        chunks,
    )

    retained = b"".join(chunks)
    assert len(retained) == probe.MAX_PROBE_BYTES + 1


def test_drain_treats_a_closed_stream_as_end_of_input() -> None:
    class ClosedStream:
        def read(self, _size: int) -> bytes:
            raise ValueError("read of closed file")

    chunks: list[bytes] = []

    probe._drain(ClosedStream(), probe.MAX_PROBE_BYTES, chunks)  # type: ignore[arg-type]

    assert chunks == []


def test_collect_probe_rejects_duplicate_json_keys(tmp_path: Path) -> None:
    executable = _make_executable(
        tmp_path,
        b'{"schema_version":"envlens.probe/v1","schema_version":"envlens.probe/v1"}',
    )

    with pytest.raises(probe.ProbeError) as caught:
        probe.collect_probe(executable, timeout_seconds=3)

    assert caught.value.code == "invalid-probe-json"
    assert "duplicate key 'schema_version'" in caught.value.message


def test_collect_probe_rejects_non_finite_json_numbers(tmp_path: Path) -> None:
    executable = _make_executable(
        tmp_path,
        b'{"schema_version":"envlens.probe/v1","number":NaN}',
    )

    with pytest.raises(probe.ProbeError) as caught:
        probe.collect_probe(executable, timeout_seconds=3)

    assert caught.value.code == "invalid-probe-json"
    assert "non-finite number NaN" in caught.value.message


def test_collect_probe_rejects_probe_output_over_limit(tmp_path: Path) -> None:
    executable = _make_executable(tmp_path)
    with (
        patch.object(
            probe,
            "_run_bounded",
            return_value=(b"x" * (probe.MAX_PROBE_BYTES + 1), b"", 0),
        ),
        pytest.raises(probe.ProbeError) as caught,
    ):
        probe.collect_probe(executable, timeout_seconds=3)

    assert caught.value.code == "probe-output-too-large"


def test_collect_probe_reports_nonzero_exit_and_bounded_stderr(tmp_path: Path) -> None:
    executable = _make_executable(tmp_path)
    stderr = b"failure details"
    with (
        patch.object(
            probe,
            "_run_bounded",
            return_value=(b"", stderr, 23),
        ),
        pytest.raises(probe.ProbeError) as caught,
    ):
        probe.collect_probe(executable, timeout_seconds=3)

    assert caught.value.code == "probe-failed"
    assert "interpreter exited 23" in caught.value.message
    assert "stderr bytes=15" in caught.value.message
    assert "failure details" not in caught.value.message


def test_run_bounded_terminates_timed_out_process() -> None:
    with pytest.raises(probe.ProbeError) as caught:
        probe._run_bounded(
            [sys.executable, "-c", "import time; time.sleep(30)"],
            timeout_seconds=1,
        )

    assert caught.value.code == "probe-timeout"
    assert "exceeded 1 seconds" in caught.value.message


def test_run_bounded_does_not_wait_forever_for_inherited_pipe_fds() -> None:
    child_script = "import time; time.sleep(30)"
    parent_script = (
        "import subprocess,sys; "
        f"subprocess.Popen([sys.executable, '-c', {child_script!r}], "
        "stdout=sys.stdout, stderr=sys.stderr); "
        "print('done')"
    )

    started = time.monotonic()
    with patch.object(probe, "READER_DRAIN_SECONDS", 0.1):
        stdout, stderr, return_code = probe._run_bounded(
            [sys.executable, "-c", parent_script], timeout_seconds=3
        )

    assert time.monotonic() - started < 2
    assert stdout == b"done\n"
    assert stderr == b""
    assert return_code == 0


@pytest.mark.skipif(os.name != "posix", reason="detached POSIX session regression")
@pytest.mark.parametrize("parent_waits", [False, True])
def test_detached_descendant_cannot_block_pipe_cleanup(tmp_path: Path, parent_waits: bool) -> None:
    pid_file = tmp_path / "child.pid"
    parent = (
        "import subprocess,sys,time,pathlib; "
        "child=subprocess.Popen([sys.executable,'-c','import time; time.sleep(30)'], "
        "start_new_session=True); "
        f"pathlib.Path({str(pid_file)!r}).write_text(str(child.pid)); "
        "print('done',flush=True); " + ("time.sleep(30)" if parent_waits else "pass")
    )
    wrapper = (
        "import sys,threading; from envlens import probe; "
        "probe.READER_DRAIN_SECONDS=0.1; "
        "before=threading.active_count(); "
        f"command=[sys.executable,'-c',{parent!r}]\n"
        "try:\n"
        " result=probe._run_bounded(command,1)\n"
        " assert result==(b'done\\n',b'',0),result\n"
        "except probe.ProbeError as error:\n"
        f" assert {parent_waits!r} and error.code=='probe-timeout'\n"
        "assert threading.active_count()==before\n"
    )
    try:
        result = subprocess.run([sys.executable, "-c", wrapper], capture_output=True, timeout=5)
        assert result.returncode == 0, result.stderr.decode()
    finally:
        if pid_file.exists():
            with suppress(ProcessLookupError):
                os.kill(int(pid_file.read_text()), signal.SIGKILL)


@pytest.mark.skipif(not hasattr(os, "environb"), reason="byte environments are POSIX-only")
def test_probe_preserves_non_utf8_environment_bytes_as_safe_json() -> None:
    name = b"ENVLENS_NON_UTF8_TEST"
    previous = os.environb.get(name)
    os.environb[name] = b"\xff"
    try:
        payload, _, _ = probe.collect_probe(sys.executable, timeout_seconds=10)
    finally:
        if previous is None:
            os.environb.pop(name, None)
        else:
            os.environb[name] = previous

    assert payload["environment"][name.decode()] == "\udcff"


def test_resolve_interpreter_returns_real_target_for_symlink(tmp_path: Path) -> None:
    target = tmp_path / "python-target"
    target.write_bytes(Path(sys.executable).read_bytes())
    target.chmod(target.stat().st_mode | stat.S_IXUSR)
    link = tmp_path / "python-link"
    try:
        link.symlink_to(target)
    except (NotImplementedError, OSError):
        pytest.skip("symbolic links are unavailable")

    resolved, requested = probe.resolve_interpreter(link)

    assert requested == str(link)
    assert resolved == target
    assert resolved.is_file()
    assert os.access(resolved, os.X_OK)
