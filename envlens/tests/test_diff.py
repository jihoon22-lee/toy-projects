"""E2 snapshot diff and offline compatibility tests."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from envlens.diff import (
    DiffError,
    _marker_matches,
    _wheel_tag_match,
    check_compatibility,
    compare_snapshots,
    compare_versions,
    load_snapshot,
    satisfies_requires_python,
)
from envlens.report import dumps_report, render_markdown, render_text


def _distribution(
    name: str,
    version: str,
    *,
    imports: list[str] | None = None,
    requirements: list[str] | None = None,
    requires_python: str = ">=3.10",
    wheel_tags: list[str] | None = None,
) -> dict[str, object]:
    metadata: dict[str, object] = {
        "requires_python": requires_python,
        "requires_dist": [] if requirements is None else requirements,
    }
    if wheel_tags is not None:
        metadata["wheel_tags"] = wheel_tags
    distribution: dict[str, object] = {
        "name": name,
        "normalized_name": name.replace("_", "-").lower(),
        "version": version,
        "metadata": metadata,
        "entry_points": [],
        "location": f"/site/{name}",
        "status": "ok",
        "errors": [],
    }
    if imports is not None:
        distribution["import_names"] = imports
    return distribution


def _snapshot(
    distributions: list[dict[str, object]], *, complete: bool = True
) -> dict[str, object]:
    return {
        "schema_version": "envlens.snapshot/v1",
        "producer": {"name": "envlens", "version": "0.1.0"},
        "captured_at": "2026-09-03T00:00:00Z",
        "redaction": {"policy": "envlens-redaction/v1", "enabled": True},
        "source": {
            "kind": "python-interpreter",
            "requested_executable": "/usr/bin/python",
            "resolved_executable": "/usr/bin/python",
            "identity": {
                "implementation": "cpython",
                "version": "3.10.12",
                "version_info": [3, 10, 12, "final", 0],
                "cache_tag": "cpython-310",
                "platform": "linux",
                "machine": "x86_64",
            },
        },
        "environment": {"variables": {}},
        "distributions": distributions,
        "collection": {
            "status": "complete" if complete else "partial",
            "distribution_count": len(distributions),
            "error_count": 0 if complete else 1,
        },
    }


def test_diff_classifies_version_and_presence_changes() -> None:
    before = _snapshot(
        [
            _distribution("alpha_pkg", "1.0", imports=["alpha"]),
            _distribution("down", "3.0"),
            _distribution("removed", "1.0"),
        ]
    )
    after = _snapshot(
        [
            _distribution("alpha-pkg", "2.0", imports=["alpha", "alpha_plugins"]),
            _distribution("down", "2.0"),
            _distribution("added", "1.0"),
        ]
    )

    result = compare_snapshots(before, after)

    assert result["status"] == "changed"
    assert [item["name"] for item in result["added"]] == ["added"]
    assert [item["name"] for item in result["removed"]] == ["removed"]
    assert result["upgraded"][0]["from_version"] == "1.0"
    assert result["upgraded"][0]["to_version"] == "2.0"
    assert result["downgraded"][0]["from_version"] == "3.0"
    assert result["import_name_changes"][0]["normalized_project_name"] == "alpha-pkg"
    assert result["import_name_changes"][0]["before"] == ["alpha"]
    assert result["import_name_changes"][0]["after"] == ["alpha", "alpha_plugins"]


def test_diff_reports_offline_dependency_and_compatibility_evidence() -> None:
    after = _snapshot(
        [
            _distribution(
                "consumer",
                "1.0",
                requirements=["missing>=1", "present>=2"],
                wheel_tags=["cp311-cp311-win_amd64"],
            ),
            _distribution("present", "1.0"),
        ],
        complete=False,
    )
    project = {
        "project": {
            "requires_python": ">=3.11",
            "dependencies": ["present>=2"],
        }
    }
    result = compare_snapshots(_snapshot([]), after, project=project)

    assert any(
        item["kind"] == "missing" and item["name"] == "missing" for item in result["dependencies"]
    )
    assert any(
        item["kind"] == "version-conflict" and item["name"] == "present"
        for item in result["dependencies"]
    )
    project_check = next(item for item in result["compatibility"] if item["name"] == "project")
    assert project_check["status"] == "incompatible"
    wheel_check = next(item for item in result["compatibility"] if item["kind"] == "wheel")
    assert wheel_check["status"] == "incompatible"
    assert all("certainty" in item for item in result["dependencies"] + result["compatibility"])
    assert result["status"] == "incompatible"


def test_compatibility_helpers_cover_unknown_and_wildcard_cases() -> None:
    identity = {"version": "3.10.12", "version_info": [3, 10, 12, "final", 0]}
    assert satisfies_requires_python(">=3.10,<4", identity) is True
    assert satisfies_requires_python("~=3.11", identity) is False
    assert satisfies_requires_python("not-a-specifier", identity) is None
    assert compare_versions("1.0", "1.0.0") == 0
    assert compare_versions("not-a-version", "1.0") is None


def test_version_evaluator_is_conservative_for_non_numeric_pep440_forms() -> None:
    identity = {"version": "3.10.12", "version_info": [3, 10, 12, "final", 0]}
    for value in ("1.0rc1", "1.0.dev1", "1.0.post1", "1.0+local", "1!1.0"):
        assert compare_versions(value, "1.0") is None
    for expression in (">=3.10rc1", ">=3.10.post1", ">=3.10+local", ">=1!3.10"):
        assert satisfies_requires_python(expression, identity) is None
    assert satisfies_requires_python("===3.10", identity) is None
    assert satisfies_requires_python("~=3", identity) is None
    assert compare_versions("9" * 100_000, "1.0") is None


def test_marker_versions_use_numeric_ordering_and_unknown_unsupported_logic() -> None:
    identity = {
        "version": "3.10.12",
        "version_info": [3, 10, 12, "final", 0],
        "platform": "linux",
        "machine": "x86_64",
    }
    assert _marker_matches("python_version < '3.9'", identity) is False
    assert _marker_matches("python_version >= '3.10' and sys_platform == 'linux'", identity)
    assert _marker_matches("python_version < '3.9' or sys_platform == 'linux'", identity) is None
    assert _marker_matches("python_version >= '3.10rc1'", identity) is None
    assert _marker_matches("sys_platform in 'linux,win32'", identity) is None


def test_wheel_matching_is_fail_safe_for_platform_abi_and_compound_tags() -> None:
    linux = {
        "implementation": "cpython",
        "version": "3.10.12",
        "version_info": [3, 10, 12, "final", 0],
        "platform": "linux",
        "machine": "x86_64",
    }
    windows = {**linux, "platform": "win32", "machine": "AMD64"}
    mac = {**linux, "platform": "darwin"}
    assert _wheel_tag_match("py3-none-any", linux) is True
    assert _wheel_tag_match("cp310-cp310-linux_x86_64", linux) is True
    assert _wheel_tag_match("cp310-cp310-win_amd64", windows) is True
    assert _wheel_tag_match("cp310-cp310-win_arm64", windows) is False
    assert _wheel_tag_match("cp310-cp310-manylinux_2_17_x86_64", linux) is None
    assert _wheel_tag_match("cp39-abi3-manylinux_2_17_x86_64", linux) is None
    assert _wheel_tag_match("cp310.cp311-cp310-linux_x86_64", linux) is None
    assert _wheel_tag_match("cp310-cp310-macosx_11_0_x86_64", mac) is None


def test_duplicate_normalized_distributions_are_an_ambiguous_change() -> None:
    before = _snapshot([_distribution("duplicate", "1.0"), _distribution("duplicate", "2.0")])
    after = _snapshot([_distribution("duplicate", "3.0")])

    result = compare_snapshots(before, after)

    assert result["upgraded"] == []
    assert result["changed"][0]["certainty"] == "unknown"
    assert "ambiguous" in result["changed"][0]["reason"]


def test_single_snapshot_compatibility_helper_is_offline_and_public() -> None:
    report = check_compatibility(_snapshot([_distribution("one", "1.0")]))

    assert report["schema_version"] == "envlens.compatibility/v1"
    assert report["status"] == "compatible"
    assert report["dependencies"] == []


def test_diff_reports_are_canonical_and_have_all_requested_formats(tmp_path: Path) -> None:
    report = compare_snapshots(
        _snapshot([_distribution("one", "1.0")]),
        _snapshot([_distribution("one", "1.1")]),
    )
    compact = dumps_report(report)

    assert json.loads(compact) == report
    assert compact == dumps_report(json.loads(compact))
    assert "envlens diff: CHANGED" in render_text(report)
    markdown = render_markdown(report)
    assert "| Category | Project | Version | Certainty |" in markdown
    output = tmp_path / "report.json"
    output.write_text(compact, encoding="utf-8")
    assert json.loads(output.read_text(encoding="utf-8")) == report


def test_load_snapshot_rejects_duplicate_keys_and_bad_contract(tmp_path: Path) -> None:
    duplicate = tmp_path / "duplicate.json"
    duplicate.write_text(
        '{"schema_version":"envlens.snapshot/v1","schema_version":"x"}', encoding="utf-8"
    )
    with pytest.raises(DiffError, match="duplicate key"):
        load_snapshot(duplicate)
    with pytest.raises(DiffError, match=r"expected envlens\.snapshot/v1"):
        compare_snapshots({"schema_version": "wrong"}, _snapshot([]))
