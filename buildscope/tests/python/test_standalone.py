from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
import zipfile
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from unittest import mock

from tools.build_standalone import build_standalone, main


class StandaloneReleaseTests(unittest.TestCase):
    project_root: Path
    builder: Path
    database: Path

    @classmethod
    def setUpClass(cls) -> None:
        cls.project_root = Path(__file__).resolve().parents[2]
        cls.builder = cls.project_root / "tools/build_standalone.py"
        cls.database = cls.project_root / "fixtures/compile_commands.json"

    def _build(self, output: Path) -> str:
        version, digest = build_standalone(self.project_root, output)
        self.assertEqual(version, "0.5.0")
        self.assertEqual(digest, hashlib.sha256(output.read_bytes()).hexdigest())
        return digest

    def test_archive_is_reproducible_and_contains_public_schemas(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first = root / "first.pyz"
            second = root / "second.pyz"

            self.assertEqual(self._build(first), self._build(second))
            with zipfile.ZipFile(first) as archive:
                names = set(archive.namelist())

        self.assertIn("__main__.py", names)
        self.assertIn("buildscope/__main__.py", names)
        self.assertEqual(
            sorted(name for name in names if name.startswith("buildscope/schemas/")),
            [
                "buildscope/schemas/buildscope-diff-v1.schema.json",
                "buildscope/schemas/buildscope-snapshot-v1.schema.json",
                "buildscope/schemas/buildscope-snapshot-v2.schema.json",
                "buildscope/schemas/buildscope-snapshot-v3.schema.json",
            ],
        )

    def test_archive_executes_without_installing_the_package(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "buildscope.pyz"
            self._build(archive)
            result = subprocess.run(
                [
                    sys.executable,
                    str(archive),
                    str(self.database),
                    "--project-root",
                    str(self.project_root),
                ],
                cwd=temporary,
                check=True,
                capture_output=True,
                text=True,
            )

        payload = json.loads(result.stdout)
        self.assertEqual(payload["schema_version"], "buildscope.snapshot/v2")
        self.assertEqual(payload["producer"]["version"], "0.5.0")
        self.assertEqual(payload["source"]["entry_count"], 2)

    def test_archive_reports_its_version_without_a_database_argument(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "buildscope.pyz"
            self._build(archive)
            result = subprocess.run(
                [sys.executable, str(archive), "--version"],
                cwd=temporary,
                check=True,
                capture_output=True,
                text=True,
            )

        self.assertEqual(result.stdout, "buildscope 0.5.0\n")
        self.assertEqual(result.stderr, "")

    def test_builder_cli_reports_the_version_output_and_digest(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "buildscope.pyz"
            stdout = StringIO()
            with (
                mock.patch.object(
                    sys,
                    "argv",
                    [
                        str(self.builder),
                        "--project-root",
                        str(self.project_root),
                        "--output",
                        str(output),
                    ],
                ),
                redirect_stdout(stdout),
            ):
                self.assertEqual(main(), 0)

            digest = hashlib.sha256(output.read_bytes()).hexdigest()

        self.assertIn("built BuildScope 0.5.0", stdout.getvalue())
        self.assertIn(f"sha256:{digest}", stdout.getvalue())

    def test_builder_refuses_to_replace_a_symlink(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "buildscope.pyz"
            try:
                output.symlink_to(root / "redirected.pyz")
            except OSError as error:
                self.skipTest(f"symlink creation is unavailable: {error}")

            with self.assertRaisesRegex(ValueError, "refusing to replace symlink"):
                build_standalone(self.project_root, output)


if __name__ == "__main__":
    unittest.main()
