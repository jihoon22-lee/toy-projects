"""Regression evidence for standards, environment identity and runtime execution."""

from __future__ import annotations

import json
import subprocess
import sys
import venv
from pathlib import Path

import pytest
from test_diff import _distribution, _snapshot

from envlens.__main__ import main
from envlens.diff import _wheel_tag_match, check_compatibility, compare_snapshots
from envlens.runtime import run_runtime_checks


def test_transitive_extras_and_dependency_explanation() -> None:
    snapshot = _snapshot(
        [
            _distribution("outer", "1", requirements=["inner[fast]>=1"]),
            _distribution("inner", "1", requirements=['missing>=2; extra == "fast"']),
        ]
    )
    report = check_compatibility(snapshot, project={"dependencies": ["outer"]})
    issue = next(item for item in report["dependencies"] if item["name"] == "missing")
    assert issue["dependency_path"] == ["project", "outer", "inner", "missing"]
    assert report["status"] == "incompatible"


def test_direct_url_requires_unredacted_origin() -> None:
    package = _distribution("demo", "1")
    snapshot = _snapshot([package])
    project = {"dependencies": ["demo @ https://example.org/demo.whl"]}
    assert check_compatibility(snapshot, project=project)["status"] == "unknown"
    package["origin"] = {"available": True, "url": "https://example.org/demo.whl"}
    assert not check_compatibility(snapshot, project=project)["dependencies"]
    package["origin"]["redacted"] = True
    assert check_compatibility(snapshot, project=project)["status"] == "unknown"


def test_origin_change_and_import_overlap() -> None:
    before = _snapshot([_distribution("demo", "1")])
    package = _distribution("demo", "1", imports=["shared"])
    package["origin"] = {"available": True, "url": "file:///other", "editable": True}
    after = _snapshot([package, _distribution("other", "1", imports=["shared"])])
    report = compare_snapshots(before, after)
    assert report["changed"][0]["reason"] == "installation origin or location changed"
    assert any(item["kind"] == "import-overlap" for item in report["dependencies"])
    assert report["runtime_suggestions"][0]["executed"] is False


def test_target_wheel_platform_evidence() -> None:
    target = {
        "version_info": [3, 11, 0],
        "implementation": "cpython",
        "platform": "linux",
        "machine": "x86_64",
        "libc_name": "glibc",
        "libc_version": "2.31",
    }
    assert _wheel_tag_match("py310-none-any", target) is True
    assert _wheel_tag_match("cp310-abi3-manylinux_2_17_x86_64", target) is True
    assert _wheel_tag_match("cp311-cp311-manylinux_2_34_x86_64", target) is False
    assert _wheel_tag_match("cp311-cp311-musllinux_1_2_x86_64", target) is False
    assert _wheel_tag_match("cp311-abi3-any", {**target, "free_threaded": True}) is False


def test_relative_compile_root_detects_syntax_error(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    root = tmp_path / "project"
    root.mkdir()
    (root / "bad.py").write_text("def broken(:\n")
    monkeypatch.chdir(tmp_path)
    report = run_runtime_checks("project", imports=[], interpreters=[sys.executable])
    assert report["summary"]["status"] == "failed"


def test_runtime_preserves_venv_site_packages(tmp_path: Path) -> None:
    environment = tmp_path / "venv"
    venv.EnvBuilder(with_pip=False, symlinks=True).create(environment)
    interpreter = environment / "bin/python"
    site = subprocess.check_output(
        [str(interpreter), "-c", "import sysconfig; print(sysconfig.get_path('purelib'))"],
        text=True,
    ).strip()
    Path(site, "only_in_venv.py").write_text("VALUE = 1\n")
    project = tmp_path / "project"
    project.mkdir()
    report = run_runtime_checks(
        project, imports=["only_in_venv"], compile_paths=[], interpreters=[interpreter]
    )
    assert report["summary"]["status"] == "passed"


def test_entry_point_string_return_is_failure(tmp_path: Path) -> None:
    (tmp_path / "pyproject.toml").write_text(
        '[project]\nname="demo"\nversion="1"\n[project.scripts]\ndemo="demo:main"\n'
    )
    (tmp_path / "demo.py").write_text('def main():\n    return "failure message"\n')
    report = run_runtime_checks(
        tmp_path,
        imports=[],
        compile_paths=[],
        interpreters=[sys.executable],
        execute_entry_points=True,
    )
    assert report["summary"]["status"] == "failed"


def test_cli_policy_and_input_alias_protection(tmp_path: Path) -> None:
    source = tmp_path / "source.json"
    source.write_text(json.dumps(_snapshot([_distribution("demo", "1", requirements=["missing"])])))
    assert main(["check", str(source), "--fail-on", "never"]) == 0
    assert main(["check", str(source), "--fail-on", "incompatible"]) == 1
    alias = tmp_path / "alias.json"
    alias.symlink_to(source)
    original = source.read_bytes()
    with pytest.raises(SystemExit) as error:
        main(["check", str(source), "--output", str(alias)])
    assert error.value.code == 2
    assert source.read_bytes() == original


def test_project_shadowing_is_static_unknown(tmp_path: Path) -> None:
    from envlens.project import inspect_pyproject

    (tmp_path / "pyproject.toml").write_text('[project]\nname="my-project"\nversion="1"\n')
    (tmp_path / "requests.py").write_text('raise RuntimeError("must not import")\n')
    snapshot = _snapshot([_distribution("requests", "2.0", imports=["requests"])])
    report = check_compatibility(snapshot, project=inspect_pyproject(tmp_path / "pyproject.toml"))
    issue = next(item for item in report["dependencies"] if item["kind"] == "project-shadowing")
    assert issue["certainty"] == "unknown"
    assert issue["source"].endswith("requests.py")


def test_compile_batches_reach_later_syntax_failure(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    from envlens import runtime

    monkeypatch.setattr(runtime, "MAX_COMPILE_COMMAND_CHARS", 1200)
    for index in range(30):
        (tmp_path / f"module_{index:03d}.py").write_text("VALUE = 1\n")
    (tmp_path / "z_bad.py").write_text("def broken(:\n")
    report = run_runtime_checks(tmp_path, imports=[], interpreters=[sys.executable])
    compile_check = next(
        item for item in report["interpreters"][0]["checks"] if item["kind"] == "compileall"
    )
    assert compile_check["batch_count"] > 1
    assert report["summary"]["status"] == "failed"
