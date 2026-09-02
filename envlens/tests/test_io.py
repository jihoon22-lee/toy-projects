"""Atomic output safety tests."""

from __future__ import annotations

import os
from pathlib import Path
from unittest.mock import patch

import pytest

from envlens import io as io_module
from envlens.snapshot import SnapshotError

SNAPSHOT = {"schema_version": "envlens.snapshot/v1", "value": "new"}


def test_write_snapshot_replaces_atomically_with_private_file(tmp_path: Path) -> None:
    output = tmp_path / "snapshot.json"
    output.write_text("old contents", encoding="utf-8")
    output.chmod(0o644)

    io_module.write_snapshot(SNAPSHOT, output)

    assert output.read_text(encoding="utf-8") == (
        '{"schema_version":"envlens.snapshot/v1","value":"new"}\n'
    )
    assert output.stat().st_mode & 0o777 == 0o600
    assert list(tmp_path.glob(".snapshot.json.*.tmp")) == []


def test_serialization_failure_leaves_existing_output_untouched(tmp_path: Path) -> None:
    output = tmp_path / "snapshot.json"
    original = "keep this exact file"
    output.write_text(original, encoding="utf-8")

    with (
        patch.object(io_module, "dumps_snapshot", side_effect=ValueError("cannot serialize")),
        pytest.raises(ValueError, match="cannot serialize"),
    ):
        io_module.write_snapshot(SNAPSHOT, output)

    assert output.read_text(encoding="utf-8") == original
    assert list(tmp_path.glob(".snapshot.json.*.tmp")) == []


def test_atomic_write_failure_leaves_existing_output_untouched(tmp_path: Path) -> None:
    output = tmp_path / "snapshot.json"
    original = "keep this exact file"
    output.write_text(original, encoding="utf-8")

    with (
        patch.object(io_module.os, "replace", side_effect=OSError("replace denied")),
        pytest.raises(SnapshotError) as caught,
    ):
        io_module.write_snapshot(SNAPSHOT, output)

    assert caught.value.code == "atomic-write-failed"
    assert "replace denied" in caught.value.message
    assert output.read_text(encoding="utf-8") == original
    assert list(tmp_path.glob(".snapshot.json.*.tmp")) == []


def test_write_snapshot_refuses_output_symlink_without_touching_target(tmp_path: Path) -> None:
    target = tmp_path / "target.json"
    target.write_text("target remains", encoding="utf-8")
    link = tmp_path / "snapshot.json"
    try:
        link.symlink_to(target)
    except (NotImplementedError, OSError):
        pytest.skip("symbolic links are unavailable")

    with pytest.raises(SnapshotError) as caught:
        io_module.write_snapshot(SNAPSHOT, link)

    assert caught.value.code == "invalid-output"
    assert target.read_text(encoding="utf-8") == "target remains"


def test_write_snapshot_refuses_symlinked_parent(tmp_path: Path) -> None:
    real_parent = tmp_path / "real-parent"
    real_parent.mkdir()
    link_parent = tmp_path / "link-parent"
    try:
        link_parent.symlink_to(real_parent, target_is_directory=True)
    except (NotImplementedError, OSError):
        pytest.skip("symbolic links are unavailable")

    with pytest.raises(SnapshotError) as caught:
        io_module.write_snapshot(SNAPSHOT, link_parent / "snapshot.json")

    assert caught.value.code == "invalid-output"
    assert list(real_parent.iterdir()) == []


def test_write_snapshot_refuses_fifo_special_file(tmp_path: Path) -> None:
    fifo = tmp_path / "snapshot.fifo"
    if not hasattr(os, "mkfifo"):
        pytest.skip("FIFO special files are unavailable")
    os.mkfifo(fifo)

    with pytest.raises(SnapshotError) as caught:
        io_module.write_snapshot(SNAPSHOT, fifo)

    assert caught.value.code == "invalid-output"
    assert fifo.exists()
