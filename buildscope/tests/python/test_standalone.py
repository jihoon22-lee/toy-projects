from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


class StandaloneReleaseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.project_root = Path(__file__).resolve().parents[2]
        cls.builder = cls.project_root / "tools/build_standalone.py"
        cls.database = cls.project_root / "fixtures/compile_commands.json"

    def _build(self, output: Path) -> str:
        result = subprocess.run(
            [sys.executable, str(self.builder), "--output", str(output)],
            cwd=self.project_root,
            check=True,
            capture_output=True,
            text=True,
        )
        self.assertIn("BuildScope 0.5.0", result.stdout)
        return hashlib.sha256(output.read_bytes()).hexdigest()

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


if __name__ == "__main__":
    unittest.main()
