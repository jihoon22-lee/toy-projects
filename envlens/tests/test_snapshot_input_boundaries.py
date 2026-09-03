"""Snapshot diff input validation and bounded-reader tests."""

from __future__ import annotations

import io
from pathlib import Path
from unittest.mock import patch

import pytest

from envlens import snapshot_input


def _snapshot(*, distributions: list[dict[str, object]] | None = None) -> dict[str, object]:
    return {
        "schema_version": "envlens.snapshot/v1",
        "source": {"identity": {"version": "3.10.12"}},
        "distributions": [] if distributions is None else distributions,
    }


def _distribution() -> dict[str, object]:
    return {
        "name": "sample",
        "normalized_name": "sample",
        "version": "1.0",
        "metadata": {"requires_python": ">=3.10", "requires_dist": []},
        "entry_points": [],
        "errors": [],
    }


def test_input_helpers_reject_wrong_types_and_bounds() -> None:
    with pytest.raises(snapshot_input.DiffError, match="must be an object"):
        snapshot_input._object([], "value")
    assert snapshot_input._object({"ok": True}, "value")["ok"] is True
    assert snapshot_input._string("", "value") == ""
    with pytest.raises(snapshot_input.DiffError, match="must be a string"):
        snapshot_input._string(1, "value")
    with pytest.raises(snapshot_input.DiffError, match="must be a string"):
        snapshot_input._string("", "value", allow_empty=False)
    with (
        patch.object(snapshot_input, "MAX_STRING_LENGTH", 1),
        pytest.raises(snapshot_input.DiffError, match="exceeds 65536"),
    ):
        snapshot_input._string("too long", "value")
    assert snapshot_input._array([], "items") == []
    with pytest.raises(snapshot_input.DiffError, match="must be an array"):
        snapshot_input._array({}, "items")
    with pytest.raises(snapshot_input.DiffError, match="exceeds 1 items"):
        snapshot_input._array([1, 2], "items", maximum=1)
    with pytest.raises(snapshot_input.DiffError, match="duplicate key"):
        snapshot_input._reject_duplicates([("a", 1), ("a", 2)])
    with pytest.raises(snapshot_input.DiffError, match="non-finite number NaN"):
        snapshot_input._reject_constant("NaN")


def test_load_snapshot_accepts_streams_and_rejects_stream_boundaries() -> None:
    assert snapshot_input.load_snapshot(
        io.StringIO(
            '{"schema_version":"envlens.snapshot/v1",'
            '"source":{"identity":{"version":"3"}},'
            '"distributions":[]}'
        )
    )["schema_version"] == ("envlens.snapshot/v1")

    class FailingStream:
        def read(self, _size: int) -> str:
            raise OSError("stream failed")

    with pytest.raises(snapshot_input.DiffError, match="snapshot-read-failed"):
        snapshot_input.load_snapshot(FailingStream())  # type: ignore[arg-type]

    class BinaryStream:
        def read(self, _size: int) -> bytes:
            return b"{}"

    with pytest.raises(snapshot_input.DiffError, match="must return text"):
        snapshot_input.load_snapshot(BinaryStream())  # type: ignore[arg-type]

    with (
        patch.object(snapshot_input, "MAX_INPUT_BYTES", 1),
        pytest.raises(snapshot_input.DiffError, match="snapshot exceeds 16 MiB"),
    ):
        snapshot_input.load_snapshot(io.StringIO("{}"))
    with pytest.raises(snapshot_input.DiffError, match="invalid-snapshot-json"):
        snapshot_input.load_snapshot(io.StringIO("not json"))
    with pytest.raises(snapshot_input.DiffError, match="invalid-snapshot-json"):
        snapshot_input.load_snapshot(
            io.StringIO('{"schema_version":"envlens.snapshot/v1","schema_version":"other"}')
        )
    with pytest.raises(snapshot_input.DiffError, match="non-finite number"):
        snapshot_input.load_snapshot(
            io.StringIO(
                '{"schema_version":"envlens.snapshot/v1","source":{"identity":'
                '{"version":"3"}},"distributions":[],"value":NaN}'
            )
        )


def test_load_snapshot_path_errors_encoding_and_post_read_limit(tmp_path: Path) -> None:
    missing = tmp_path / "missing.json"
    with pytest.raises(snapshot_input.DiffError, match="snapshot-read-failed"):
        snapshot_input.load_snapshot(missing)
    directory = tmp_path / "directory"
    directory.mkdir()
    with pytest.raises(snapshot_input.DiffError, match="regular file"):
        snapshot_input.load_snapshot(directory)
    invalid = tmp_path / "invalid.json"
    invalid.write_bytes(b"\xff")
    with pytest.raises(snapshot_input.DiffError, match="not UTF-8"):
        snapshot_input.load_snapshot(invalid)

    small = tmp_path / "small.json"
    small.write_text("x", encoding="utf-8")
    with (
        patch.object(snapshot_input, "MAX_INPUT_BYTES", 1),
        pytest.raises(snapshot_input.DiffError, match="invalid-snapshot-json"),
    ):
        snapshot_input.load_snapshot(small)

    with (
        patch.object(snapshot_input.Path, "read_bytes", side_effect=OSError("read failed")),
        pytest.raises(snapshot_input.DiffError, match="read failed"),
    ):
        snapshot_input.load_snapshot(small)
    with (
        patch.object(snapshot_input.Path, "read_bytes", return_value=b"xx"),
        patch.object(snapshot_input, "MAX_INPUT_BYTES", 1),
        pytest.raises(snapshot_input.DiffError, match="snapshot exceeds 16 MiB"),
    ):
        snapshot_input.load_snapshot(small)


@pytest.mark.parametrize(
    ("field", "value", "pattern"),
    [
        ("schema_version", "other", "expected envlens.snapshot/v1"),
        ("distributions", {}, "must be an array"),
        ("source", [], "must be an object"),
    ],
)
def test_validate_snapshot_rejects_malformed_top_level_fields(
    field: str, value: object, pattern: str
) -> None:
    snapshot = _snapshot()
    snapshot[field] = value
    with pytest.raises(snapshot_input.DiffError, match=pattern):
        snapshot_input.validate_snapshot(snapshot)


def test_validate_snapshot_checks_distribution_optional_fields() -> None:
    distribution = _distribution()
    distribution["import_names"] = ["sample"]
    distribution["metadata"]["wheel_tags"] = ["py3-none-any"]  # type: ignore[index]
    validated = snapshot_input.validate_snapshot(_snapshot(distributions=[distribution]))
    assert validated["distributions"]

    bad = _distribution()
    bad["name"] = 1
    with pytest.raises(snapshot_input.DiffError, match=r"distribution\[0\]\.name"):
        snapshot_input.validate_snapshot(_snapshot(distributions=[bad]))
    bad = _distribution()
    bad["metadata"] = []
    with pytest.raises(snapshot_input.DiffError, match="metadata must be an object"):
        snapshot_input.validate_snapshot(_snapshot(distributions=[bad]))
    bad = _distribution()
    bad["entry_points"] = {}
    with pytest.raises(snapshot_input.DiffError, match="entry_points must be an array"):
        snapshot_input.validate_snapshot(_snapshot(distributions=[bad]))
    bad = _distribution()
    bad["import_names"] = "sample"
    with pytest.raises(snapshot_input.DiffError, match="import_names must be an array"):
        snapshot_input.validate_snapshot(_snapshot(distributions=[bad]))
    bad = _distribution()
    bad["metadata"]["wheel_tags"] = {}  # type: ignore[index]
    with pytest.raises(snapshot_input.DiffError, match="wheel_tags must be an array"):
        snapshot_input.validate_snapshot(_snapshot(distributions=[bad]))
    with pytest.raises(snapshot_input.DiffError, match="identity must be an object"):
        snapshot_input.validate_snapshot(
            {"schema_version": "envlens.snapshot/v1", "distributions": [], "source": {}}
        )
