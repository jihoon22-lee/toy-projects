"""Deterministic normalization, schema shape, and collection accounting tests."""

from __future__ import annotations

import copy
import json
from datetime import datetime, timezone
from pathlib import Path
from unittest.mock import patch

import pytest

from envlens import snapshot as snapshot_module
from envlens.redaction import USER_HOME

FIXED_CAPTURED_AT = datetime(2024, 2, 3, 4, 5, 6, 789000, tzinfo=timezone.utc)


def _distribution(
    name: str,
    version: str,
    *,
    location: str,
    requirements: list[str] | None = None,
    entry_points: list[dict[str, str]] | None = None,
    errors: list[dict[str, str]] | None = None,
) -> dict[str, object]:
    return {
        "name": name,
        "version": version,
        "metadata": {
            "requires_python": ">=3.10",
            "requires_dist": [] if requirements is None else requirements,
        },
        "entry_points": [] if entry_points is None else entry_points,
        "location": location,
        "errors": [] if errors is None else errors,
    }


def _raw_probe(distributions: list[dict[str, object]]) -> dict[str, object]:
    return {
        "schema_version": "envlens.probe/v1",
        "identity": {
            "implementation": "cpython",
            "version": "3.10.14",
            "version_info": [3, 10, 14, "final", 0],
            "cache_tag": "cpython-310",
            "platform": "linux",
            "machine": "x86_64",
            "reported_executable": "/target/home/.venv/bin/python",
            "prefix": "/target/home/.venv",
            "base_prefix": "/usr",
            "exec_prefix": "/target/home/.venv",
            "compiler": "test compiler",
            "user_home": "/target/home",
        },
        "sysconfig": {
            "paths": {
                "stdlib": "/usr/lib/python3.10",
                "purelib": "/target/home/.venv/lib/python3.10/site-packages",
            },
            "variables": {"prefix": "/target/home/.venv", "SOABI": "cpython-310"},
        },
        "environment": {
            "ZED": "/target/home/work",
            "LANG": "C.UTF-8",
            "TOKEN": "must disappear",
        },
        "distributions": distributions,
    }


def _collect(raw: dict[str, object], *, redact: bool = True) -> dict[str, object]:
    with patch.object(
        snapshot_module,
        "collect_probe",
        return_value=(raw, Path("/target/home/.venv/bin/python"), "/requested/python"),
    ):
        return snapshot_module.collect_snapshot(
            "/ignored-by-patch",
            captured_at=FIXED_CAPTURED_AT,
            redact=redact,
        )


def test_snapshot_sorts_distributions_requirements_entry_points_and_errors() -> None:
    raw = _raw_probe(
        [
            _distribution(
                "zeta_pkg",
                "2.0",
                location="/target/home/zeta",
                requirements=["z-requirement", "a-requirement"],
                entry_points=[
                    {"group": "console_scripts", "name": "zeta", "value": "zeta:main"},
                    {"group": "console_scripts", "name": "alpha", "value": "alpha:main"},
                    {"group": "aaa", "name": "later", "value": "later:main"},
                ],
                errors=[
                    {"code": "z-code", "field": "z-field", "type": "ZError", "message": "z"},
                    {"code": "a-code", "field": "a-field", "type": "AError", "message": "a"},
                ],
            ),
            _distribution("Alpha...pkg", "1.0", location="/target/home/alpha"),
            _distribution("beta-pkg", "1.1", location="/other/beta"),
        ]
    )

    result = _collect(raw)

    distributions = result["distributions"]
    assert [item["normalized_name"] for item in distributions] == [
        "alpha-pkg",
        "beta-pkg",
        "zeta-pkg",
    ]
    zeta = distributions[-1]
    assert zeta["metadata"]["requires_dist"] == ["a-requirement", "z-requirement"]
    assert zeta["entry_points"] == [
        {"group": "aaa", "name": "later", "value": "later:main"},
        {"group": "console_scripts", "name": "alpha", "value": "alpha:main"},
        {"group": "console_scripts", "name": "zeta", "value": "zeta:main"},
    ]
    assert [error["code"] for error in zeta["errors"]] == ["a-code", "z-code"]
    assert result["captured_at"] == "2024-02-03T04:05:06Z"


def test_snapshot_is_deterministic_for_reordered_probe_lists() -> None:
    distributions = [
        _distribution(
            "zeta_pkg",
            "2.0",
            location="/target/home/zeta",
            requirements=["z", "a"],
            entry_points=[{"group": "g", "name": "z", "value": "z"}],
        ),
        _distribution("alpha_pkg", "1.0", location="/target/home/alpha"),
    ]
    first_raw = _raw_probe(distributions)
    second_raw = copy.deepcopy(first_raw)
    second_raw["distributions"] = list(reversed(second_raw["distributions"]))
    second_raw["distributions"][1]["metadata"]["requires_dist"] = ["a", "z"]

    first = _collect(first_raw)
    second = _collect(second_raw)

    assert first == second
    first_json = snapshot_module.dumps_snapshot(first)
    assert first_json == snapshot_module.dumps_snapshot(second)
    assert first_json.endswith("\n")
    assert json.loads(first_json) == first


def test_snapshot_is_deterministic_for_duplicate_primary_distribution_keys() -> None:
    first_distribution = _distribution(
        "duplicate",
        "1.0",
        location="/same",
        requirements=["first"],
    )
    second_distribution = _distribution(
        "duplicate",
        "1.0",
        location="/same",
        requirements=["second"],
    )

    first = _collect(_raw_probe([first_distribution, second_distribution]))
    second = _collect(_raw_probe([second_distribution, first_distribution]))

    assert first == second


def test_snapshot_redacts_url_secrets_from_every_distribution_string() -> None:
    secret_url = "https://alice:hunter2@example.invalid/path?token=visible"
    raw = _raw_probe(
        [
            _distribution(
                "safe-name",
                "1.0",
                location=secret_url,
                requirements=["dependency @ " + secret_url],
                entry_points=[{"group": "plugins", "name": "example", "value": secret_url}],
                errors=[
                    {
                        "code": "metadata-error",
                        "field": "requires_dist",
                        "type": "ValueError",
                        "message": secret_url,
                    }
                ],
            )
        ]
    )
    raw["environment"]["PIP_INDEX_URL"] = secret_url

    result = _collect(raw)
    serialized = snapshot_module.dumps_snapshot(result)

    assert "alice" not in serialized
    assert "hunter2" not in serialized
    assert "visible" not in serialized
    assert result["environment"]["variables"]["PIP_INDEX_URL"] == "<REDACTED>"


def test_snapshot_unredacted_library_mode_is_semantically_consistent() -> None:
    secret_url = "https://alice:hunter2@example.invalid/simple?token=visible"
    raw = _raw_probe(
        [
            _distribution(
                "package",
                "1.0",
                location="/target/home/package",
                requirements=["dependency @ " + secret_url],
            )
        ]
    )
    raw["environment"]["PIP_INDEX_URL"] = secret_url

    result = _collect(raw, redact=False)
    serialized = snapshot_module.dumps_snapshot(result)

    assert result["redaction"]["enabled"] is False
    assert "/target/home" in serialized
    assert "alice:hunter2" in serialized
    assert "token=visible" in serialized


def test_snapshot_preserves_partial_per_distribution_errors_and_counts() -> None:
    errors = [
        {
            "code": "metadata-error",
            "field": "metadata",
            "type": "ValueError",
            "message": "failed at /target/home/.cache/metadata",
        },
        {
            "code": "requires-error",
            "field": "requires_dist",
            "type": "RuntimeError",
            "message": "requires unavailable",
        },
    ]
    raw = _raw_probe(
        [
            _distribution(
                "broken-package",
                "0.1",
                location="/target/home/.venv/lib/broken",
                errors=errors,
            ),
            _distribution("healthy-package", "1.2", location="/usr/lib/healthy"),
        ]
    )

    result = _collect(raw)

    broken = result["distributions"][0]
    assert broken["status"] == "error"
    assert broken["location"] == USER_HOME + "/.venv/lib/broken"
    assert broken["errors"][0]["message"] == "failed at " + USER_HOME + "/.cache/metadata"
    assert result["distributions"][1]["status"] == "ok"
    assert result["collection"] == {
        "status": "partial",
        "distribution_count": 2,
        "error_count": 2,
    }


def test_snapshot_has_schema_required_keys_and_consistent_counts() -> None:
    raw = _raw_probe(
        [
            _distribution("one", "1", location="/one"),
            _distribution(
                "two",
                "2",
                location="/two",
                errors=[
                    {"code": "x", "field": "y", "type": "Z", "message": "bad"},
                ],
            ),
        ]
    )
    result = _collect(raw)
    schema_path = (
        Path(__file__).resolve().parents[1] / "schemas" / "envlens-snapshot-v1.schema.json"
    )
    schema = json.loads(schema_path.read_text(encoding="utf-8"))

    assert set(result) == set(schema["required"])
    assert result["schema_version"] == "envlens.snapshot/v1"
    assert result["redaction"] == {"policy": "envlens-redaction/v1", "enabled": True}
    assert set(result["source"]) == set(schema["properties"]["source"]["required"])
    distribution_schema = schema["properties"]["distributions"]["items"]
    for distribution in result["distributions"]:
        assert set(distribution) == set(distribution_schema["required"])
        assert set(distribution["metadata"]) == {"requires_python", "requires_dist"}
    assert result["collection"]["distribution_count"] == len(result["distributions"])
    assert result["collection"]["error_count"] == sum(
        len(distribution["errors"]) for distribution in result["distributions"]
    )


def test_collect_snapshot_rejects_nonpositive_or_boolean_timeout() -> None:
    for timeout in (0, -1, True, False, "1"):
        with pytest.raises(snapshot_module.SnapshotError) as caught:
            snapshot_module.collect_snapshot(timeout_seconds=timeout)  # type: ignore[arg-type]
        assert caught.value.code == "invalid-timeout"


def test_collect_snapshot_rejects_invalid_public_api_types_before_probing() -> None:
    for redact in (None, 1, "false"):
        with pytest.raises(snapshot_module.SnapshotError) as caught:
            snapshot_module.collect_snapshot(redact=redact)  # type: ignore[arg-type]
        assert caught.value.code == "invalid-redact"
    with pytest.raises(snapshot_module.SnapshotError) as caught:
        snapshot_module.collect_snapshot(captured_at=object())  # type: ignore[arg-type]
    assert caught.value.code == "invalid-captured-at"


def test_collect_snapshot_translates_probe_error() -> None:
    from envlens.probe import ProbeError

    with (
        patch.object(
            snapshot_module,
            "collect_probe",
            side_effect=ProbeError("probe-failed", "deliberate failure"),
        ),
        pytest.raises(snapshot_module.SnapshotError) as caught,
    ):
        snapshot_module.collect_snapshot()

    assert caught.value.code == "probe-failed"
    assert caught.value.message == "deliberate failure"


@pytest.mark.parametrize(
    ("field", "value", "message"),
    [
        ("identity", [], "identity must be an object"),
        ("environment", [], "environment must be an object"),
        ("distributions", {}, "distributions must be an array"),
    ],
)
def test_collect_snapshot_rejects_malformed_top_level_probe_shapes(
    field: str, value: object, message: str
) -> None:
    raw = _raw_probe([])
    raw[field] = value

    with pytest.raises(snapshot_module.SnapshotError, match=message):
        _collect(raw)


def test_collect_snapshot_rejects_invalid_version_info_and_sysconfig_scalar() -> None:
    raw = _raw_probe([])
    raw["identity"]["version_info"] = [3, 10, 1]  # type: ignore[index]
    with pytest.raises(snapshot_module.SnapshotError, match=r"identity\.version_info must match"):
        _collect(raw)

    raw = _raw_probe([])
    raw["sysconfig"]["paths"] = {"bad": []}  # type: ignore[index]
    with pytest.raises(snapshot_module.SnapshotError, match="not a JSON scalar"):
        _collect(raw)


def test_collect_snapshot_records_missing_distribution_metadata() -> None:
    result = _collect(_raw_probe([_distribution("", "", location="/package")]))

    distribution = result["distributions"][0]
    assert distribution["status"] == "error"
    assert [error["field"] for error in distribution["errors"]] == ["name", "version"]


def test_collect_snapshot_rejects_naive_timestamp_and_oversized_environment() -> None:
    with pytest.raises(snapshot_module.SnapshotError) as caught:
        snapshot_module._timestamp(datetime(2024, 1, 1))
    assert caught.value.code == "invalid-captured-at"

    raw = _raw_probe([])
    raw["environment"] = {
        f"NAME_{index}": "value" for index in range(snapshot_module.MAX_ENVIRONMENT_VARIABLES + 1)
    }
    with pytest.raises(snapshot_module.SnapshotError) as caught:
        _collect(raw)
    assert caught.value.code == "probe-field-too-large"


def test_dump_is_utf8_safe_for_surrogate_escaped_environment_bytes() -> None:
    payload = {"environment": {"value": "\udcff"}}

    encoded = snapshot_module.dumps_snapshot(payload).encode("utf-8")

    assert encoded == b'{"environment":{"value":"\\udcff"}}\n'


def test_real_current_interpreter_smoke_is_structural() -> None:
    result = snapshot_module.collect_snapshot(
        Path(__import__("sys").executable),
        timeout_seconds=20,
        captured_at=FIXED_CAPTURED_AT,
    )

    assert result["schema_version"] == "envlens.snapshot/v1"
    assert result["producer"] == {"name": "envlens", "version": "0.1.0"}
    assert result["captured_at"] == "2024-02-03T04:05:06Z"
    assert isinstance(result["source"]["identity"], dict)
    assert isinstance(result["source"]["sysconfig"]["paths"], dict)
    assert isinstance(result["source"]["sysconfig"]["variables"], dict)
    assert isinstance(result["environment"]["variables"], dict)
    assert result["collection"]["distribution_count"] == len(result["distributions"])
    assert result["collection"]["error_count"] == sum(
        len(item["errors"]) for item in result["distributions"]
    )
    assert all(isinstance(item["name"], str) for item in result["distributions"])
    assert all(item["status"] in {"ok", "error"} for item in result["distributions"])
    assert json.loads(snapshot_module.dumps_snapshot(result)) == result


def test_snapshot_preserves_optional_import_and_wheel_metadata() -> None:
    raw = _raw_probe([_distribution("sample-project", "1.0", location="/site/sample")])
    distribution = raw["distributions"][0]
    distribution["import_names"] = ["sample", "sample_plugins"]  # type: ignore[index]
    distribution["metadata"]["wheel_tags"] = [  # type: ignore[index]
        "py3-none-any",
        "cp310-cp310-manylinux_2_17_x86_64",
    ]

    result = _collect(raw)

    assert result["distributions"][0]["import_names"] == ["sample", "sample_plugins"]
    assert result["distributions"][0]["metadata"]["wheel_tags"] == [
        "cp310-cp310-manylinux_2_17_x86_64",
        "py3-none-any",
    ]
