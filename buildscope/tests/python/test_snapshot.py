from __future__ import annotations

import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from buildscope.__main__ import main
from buildscope.snapshot import SCHEMA_VERSION, SnapshotError, load_compilation_database


class SnapshotTests(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.root = Path(self._temporary.name)

    def tearDown(self) -> None:
        self._temporary.cleanup()

    def _write_database(self, payload: object) -> Path:
        path = self.root / "compile_commands.json"
        path.write_text(json.dumps(payload), encoding="utf-8")
        return path

    def test_load_preserves_both_entry_forms_and_orders_by_source(self) -> None:
        database = self._write_database(
            [
                {
                    "directory": "/work/build",
                    "file": "/work/src/z.cpp",
                    "command": "c++ -DZED=1 -c /work/src/z.cpp",
                    "output": "z.o",
                },
                {
                    "directory": "/work/build",
                    "file": "/work/src/a.cpp",
                    "arguments": ["c++", "-std=c++20", "-c", "/work/src/a.cpp"],
                },
            ]
        )

        snapshot = load_compilation_database(database)

        self.assertEqual(snapshot["schema_version"], SCHEMA_VERSION)
        self.assertEqual(snapshot["source"], {"entry_count": 2, "path": str(database)})
        self.assertEqual(
            [entry["file"] for entry in snapshot["entries"]],
            ["/work/src/a.cpp", "/work/src/z.cpp"],
        )
        self.assertEqual(
            snapshot["entries"][0]["arguments"],
            ["c++", "-std=c++20", "-c", "/work/src/a.cpp"],
        )
        self.assertEqual(snapshot["entries"][1]["command"], "c++ -DZED=1 -c /work/src/z.cpp")

    def test_invalid_databases_fail_with_located_messages(self) -> None:
        cases = [
            ({"file": "main.cpp"}, "root must be an array"),
            ([{"directory": "/work", "file": "main.cpp"}], "arguments or command"),
            (
                [{"directory": "/work", "file": "", "arguments": ["c++"]}],
                "file must be a non-empty string",
            ),
            (
                [{"directory": "/work", "file": "main.cpp", "arguments": ["c++", 42]}],
                "arguments must be a non-empty string array",
            ),
            (
                [
                    {
                        "directory": "/work",
                        "file": "main.cpp",
                        "arguments": ["c++"],
                        "command": "c++ main.cpp",
                    }
                ],
                "exactly one of arguments or command",
            ),
            (
                [
                    {
                        "directory": "/work",
                        "file": "main.cpp",
                        "arguments": ["c++"],
                        "output": 42,
                    }
                ],
                "output must be a non-empty string",
            ),
        ]
        for payload, message in cases:
            with self.subTest(message=message):
                database = self._write_database(payload)
                with self.assertRaisesRegex(SnapshotError, message):
                    load_compilation_database(database)

    def test_cli_writes_a_deterministic_versioned_snapshot(self) -> None:
        database = self._write_database(
            [
                {
                    "directory": "/work",
                    "file": "/work/main.cpp",
                    "arguments": ["c++", "-c", "/work/main.cpp"],
                }
            ]
        )
        output = self.root / "snapshot.json"

        self.assertEqual(main([str(database), "--output", str(output)]), 0)

        rendered = output.read_text(encoding="utf-8")
        self.assertTrue(rendered.endswith("\n"))
        self.assertEqual(rendered, rendered.strip() + "\n")
        self.assertEqual(json.loads(rendered)["source"]["entry_count"], 1)

    def test_cli_reports_errors_without_creating_output(self) -> None:
        output = self.root / "snapshot.json"
        stderr = io.StringIO()

        with contextlib.redirect_stderr(stderr):
            result = main([str(self.root / "missing.json"), "--output", str(output)])

        self.assertEqual(result, 2)
        self.assertFalse(output.exists())
        self.assertIn("buildscope: cannot read compilation database", stderr.getvalue())

    def test_database_size_and_entry_count_are_bounded(self) -> None:
        database = self._write_database(
            [
                {"directory": "/work", "file": "a.cpp", "arguments": ["c++"]},
                {"directory": "/work", "file": "b.cpp", "arguments": ["c++"]},
            ]
        )
        with (
            mock.patch("buildscope.snapshot.MAX_DATABASE_BYTES", 8),
            self.assertRaisesRegex(SnapshotError, "exceeds 8 byte limit"),
        ):
            load_compilation_database(database)
        with (
            mock.patch("buildscope.snapshot.MAX_ENTRIES", 1),
            self.assertRaisesRegex(SnapshotError, "exceeds 1 entry limit"),
        ):
            load_compilation_database(database)


if __name__ == "__main__":
    unittest.main()
