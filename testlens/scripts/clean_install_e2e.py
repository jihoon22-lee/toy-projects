"""Install a built wheel into an isolated environment and exercise every command."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def checked(command, expected=0, **kwargs):
    result = subprocess.run(command, capture_output=True, text=True, **kwargs)
    if result.returncode != expected:
        raise RuntimeError(f"{command}: {result.returncode}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def main():
    wheels = sorted((ROOT / "dist").glob("testlens-*.whl"))
    if not wheels:
        raise SystemExit("Build first: uv build")
    artifact_root = ROOT / ".artifacts"
    artifact_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="clean-install-", dir=artifact_root) as folder:
        scratch = Path(folder)
        environment = scratch / "venv"
        checked([sys.executable, "-m", "venv", str(environment)])
        python = environment / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
        checked([str(python), "-m", "pip", "install", str(wheels[-1])])
        command = [str(python), "-m", "testlens"]
        env = dict(os.environ)
        env.pop("PYTHONPATH", None)
        for index, name in enumerate(["baseline", "current"], 1):
            checked(
                command
                + [
                    "collect",
                    str(ROOT / "examples" / f"{name}.xml"),
                    "--project",
                    "demo",
                    "--run-id",
                    name,
                    "--complete",
                    "--executed-at",
                    f"2026-01-0{index}T00:00:00Z",
                    "--output",
                    f"{name}.json",
                ],
                cwd=scratch,
                env=env,
            )
            checked(command + ["validate", f"{name}.json"], cwd=scratch, env=env)
        checked(
            command + ["diff", "baseline.json", "current.json", "--fail-on", "new-failure"],
            expected=1,
            cwd=scratch,
            env=env,
        )
        checked(command + ["history", "baseline.json", "current.json"], cwd=scratch, env=env)
        checked(
            command
            + [
                "report",
                "current.json",
                "--baseline",
                "baseline.json",
                "--history",
                "baseline.json",
                "current.json",
                "--output",
                "report.html",
            ],
            cwd=scratch,
            env=env,
        )
        assert "__DATA__" not in (scratch / "report.html").read_text()
        for test_element, expected in [
            ('<Test Status="interrupted"><Name>test</Name></Test>', 1),
            ('<Test Status="passed"/>', 1),
            ('<Test Status="notrun"><Name>disabled</Name></Test>', 0),
        ]:
            (scratch / "Test.xml").write_text(
                "<Site><Testing>"
                + test_element
                + "<EndDateTime>today</EndDateTime></Testing></Site>"
            )
            output = checked(
                command
                + [
                    "collect",
                    "Test.xml",
                    "--dialect",
                    "ctest",
                    "--project",
                    "demo",
                    "--run-id",
                    "ctest",
                    "--complete",
                    "--fail-on",
                    "incomplete",
                ],
                expected=expected,
                cwd=scratch,
                env=env,
            )
            assert json.loads(output)["complete"] == (expected == 0)
        print("Clean wheel install: collect/CTest policy → validate → diff → history → HTML passed")


if __name__ == "__main__":
    main()
