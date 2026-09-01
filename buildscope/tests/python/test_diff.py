from __future__ import annotations

import contextlib
import io
import json
import os
import tempfile
import unittest
from pathlib import Path

from buildscope.__main__ import main as buildscope_main
from buildscope.diff import DIFF_SCHEMA_VERSION, DiffError, compare_databases, dumps_diff
from buildscope.diff_policy import DiffPolicyError, parse_suppressions


class ConfigurationDiffTests(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.root = Path(self._temporary.name)

    def tearDown(self) -> None:
        self._temporary.cleanup()

    def _database(self, name: str, entries: list[dict[str, object]]) -> tuple[Path, Path]:
        project = self.root / name
        build = project / "build"
        build.mkdir(parents=True, exist_ok=True)
        path = build / "compile_commands.json"
        path.write_text(json.dumps(entries), encoding="utf-8")
        return path, project

    @staticmethod
    def _entry(
        root: Path,
        source: str,
        *flags: str,
        compiler: str = "g++",
        output_target: str = "app",
        launcher: tuple[str, ...] = (),
    ) -> dict[str, object]:
        source_path = root / source
        output = f"CMakeFiles/{output_target}.dir/{source}.o"
        return {
            "arguments": [*launcher, compiler, *flags, "-c", str(source_path), "-o", output],
            "directory": str(root / "build"),
            "file": str(source_path),
            "output": output,
        }

    def _compare(
        self,
        before_entries: list[dict[str, object]],
        after_entries: list[dict[str, object]],
        **kwargs: object,
    ) -> dict[str, object]:
        before, before_root = self._database("before", before_entries)
        after, after_root = self._database("after", after_entries)
        return compare_databases(
            before,
            after,
            before_project_root=before_root,
            after_project_root=after_root,
            **kwargs,
        )

    def test_relocation_order_output_and_define_spelling_are_semantically_equal(self) -> None:
        before_root = self.root / "before"
        after_root = self.root / "after"
        before_entries = [
            self._entry(before_root, "src/b.cpp", "-DNAME", "-I", str(before_root / "include")),
            self._entry(before_root, "src/a.cpp", "-std=c++20", output_target="old-target"),
        ]
        after_entries = [
            self._entry(after_root, "src/a.cpp", "-std=c++20", output_target="old-target"),
            self._entry(
                after_root,
                "src/b.cpp",
                "-DNAME=1",
                "-I",
                str(after_root / "include"),
            ),
        ]

        report = self._compare(before_entries, after_entries)

        self.assertEqual(report["schema_version"], DIFF_SCHEMA_VERSION)
        self.assertEqual(report["units"], [])
        self.assertEqual(report["summary"]["unchanged"], 2)
        self.assertEqual(report["summary"]["visible_units"], 0)
        self.assertEqual(
            report["inputs"]["before"]["semantic_digest"],
            report["inputs"]["after"]["semantic_digest"],
        )

    def test_structured_fields_and_residual_flags_are_reported(self) -> None:
        before_root = self.root / "before"
        after_root = self.root / "after"
        before = self._entry(
            before_root,
            "src/a.cpp",
            "-std=c++17",
            "-DONE=1",
            "-UTWO",
            "-I",
            str(before_root / "first"),
            "-isystem",
            "/sdk/include",
            "--target=x86_64-linux-gnu",
            "-O0",
            launcher=("env", "MODE=old", "ccache"),
        )
        after = self._entry(
            after_root,
            "src/a.cpp",
            "-std=c++20",
            "-UTWO",
            "-DONE=2",
            "-isystem",
            "/sdk/include",
            "-I",
            str(after_root / "first"),
            "--target=arm64-linux-gnu",
            "-O2",
            compiler="clang++",
            output_target="library",
            launcher=("env", "MODE=new", "ccache"),
        )

        report = self._compare([before], [after])
        unit = report["units"][0]

        self.assertEqual(unit["kind"], "changed")
        self.assertEqual(
            [change["category"] for change in unit["changes"]],
            ["compiler", "define", "flag", "include", "launcher", "standard", "target"],
        )
        self.assertEqual(report["summary"]["changed"], 1)
        self.assertEqual(report["summary"]["visible_changes"], 7)

    def test_added_removed_and_unique_move_are_classified_once(self) -> None:
        before_root = self.root / "before"
        after_root = self.root / "after"
        before = [
            self._entry(before_root, "src/move.cpp", "-std=c++20"),
            self._entry(before_root, "src/remove.cpp", "-DOLD=1"),
        ]
        after = [
            self._entry(after_root, "renamed/move.cpp", "-std=c++20"),
            self._entry(after_root, "src/add.cpp", "-DNEW=1"),
        ]

        report = self._compare(before, after)

        self.assertEqual([unit["kind"] for unit in report["units"]], ["moved", "added", "removed"])
        self.assertEqual(
            {key: report["summary"][key] for key in ("added", "moved", "removed")},
            {"added": 1, "moved": 1, "removed": 1},
        )
        moved = report["units"][0]
        self.assertEqual(
            moved["source"],
            {"before": "src/move.cpp", "after": "renamed/move.cpp", "style": "posix"},
        )

    def test_ambiguous_moves_remain_conservative_added_and_removed(self) -> None:
        before_root = self.root / "before"
        after_root = self.root / "after"
        before = [
            self._entry(before_root, "old/a.cpp", "-std=c++20"),
            self._entry(before_root, "old/b.cpp", "-std=c++20"),
        ]
        after = [
            self._entry(after_root, "new/a.cpp", "-std=c++20"),
            self._entry(after_root, "new/b.cpp", "-std=c++20"),
        ]

        report = self._compare(before, after)

        self.assertEqual(report["summary"]["moved"], 0)
        self.assertEqual(report["summary"]["added"], 2)
        self.assertEqual(report["summary"]["removed"], 2)

    def test_multiple_configurations_use_unique_target_roles(self) -> None:
        before_root = self.root / "before"
        after_root = self.root / "after"
        before = [
            self._entry(before_root, "src/a.cpp", "-O0", output_target="debug"),
            self._entry(before_root, "src/a.cpp", "-O2", output_target="release"),
        ]
        after = [
            self._entry(after_root, "src/a.cpp", "-O3", output_target="release"),
            self._entry(after_root, "src/a.cpp", "-O0", output_target="debug"),
        ]

        report = self._compare(before, after)

        self.assertEqual(report["summary"]["unchanged"], 1)
        self.assertEqual(report["summary"]["changed"], 1)
        self.assertEqual(report["units"][0]["changes"][0]["category"], "flag")

    def test_suppression_is_visible_and_changes_exit_policy_only(self) -> None:
        before_root = self.root / "before"
        after_root = self.root / "after"
        report = self._compare(
            [self._entry(before_root, "src/a.cpp", "-std=c++17")],
            [self._entry(after_root, "src/a.cpp", "-std=c++20")],
            suppressions=["standard:src/*.cpp"],
        )

        unit = report["units"][0]
        self.assertTrue(unit["suppressed"])
        self.assertEqual(unit["changes"][0]["suppression"], "standard:src/*.cpp")
        self.assertEqual(report["summary"]["visible_units"], 0)
        self.assertEqual(report["summary"]["suppressed_changes"], 1)
        self.assertEqual(
            report["policy"]["suppression_rules"],
            [{"category": "standard", "path": "src/*.cpp"}],
        )

    def test_invalid_and_duplicate_suppressions_fail_closed(self) -> None:
        with self.assertRaisesRegex(DiffPolicyError, "unknown suppression category"):
            parse_suppressions(["typo:*"])
        with self.assertRaisesRegex(DiffPolicyError, "duplicate suppression"):
            parse_suppressions(["standard:*", "standard:*"])
        with self.assertRaisesRegex(DiffPolicyError, "must not be empty"):
            parse_suppressions(["standard:"])

        before_root = self.root / "before"
        after_root = self.root / "after"
        with self.assertRaisesRegex(DiffError, "before label"):
            self._compare(
                [self._entry(before_root, "src/a.cpp")],
                [self._entry(after_root, "src/a.cpp")],
                before_label="",
            )

    def test_response_file_does_not_produce_a_false_clean_result(self) -> None:
        before_root = self.root / "before"
        after_root = self.root / "after"

        with self.assertRaisesRegex(DiffError, "opaque response file"):
            self._compare(
                [self._entry(before_root, "src/a.cpp", "@flags.rsp")],
                [self._entry(after_root, "src/a.cpp", "@flags.rsp")],
            )

        with self.assertRaisesRegex(DiffError, "opaque response file"):
            self._compare(
                [self._entry(before_root, "src/a.cpp", "--", "@flags.rsp")],
                [self._entry(after_root, "src/a.cpp", "--", "@flags.rsp")],
            )

    def test_windows_source_identity_is_case_insensitive(self) -> None:
        before, _ = self._database(
            "windows-before",
            [
                {
                    "arguments": [r"C:\Tools\g++.exe", "-std=c++20", "-c", r"C:\Repo\Src\A.cpp"],
                    "directory": r"C:\Repo\build",
                    "file": r"C:\Repo\Src\A.cpp",
                }
            ],
        )
        after, _ = self._database(
            "windows-after",
            [
                {
                    "arguments": [r"C:\Tools\g++.exe", "-std=c++20", "-c", r"c:\repo\src\a.cpp"],
                    "directory": r"c:\repo\build",
                    "file": r"c:\repo\src\a.cpp",
                }
            ],
        )

        report = compare_databases(
            before,
            after,
            before_project_root=r"C:\Repo",
            after_project_root=r"c:\repo",
        )

        self.assertEqual(report["summary"]["unchanged"], 1)
        self.assertEqual(report["units"], [])

    def test_windows_suppression_glob_is_case_insensitive(self) -> None:
        before, _ = self._database(
            "windows-suppress-before",
            [
                {
                    "arguments": [r"C:\Tools\g++.exe", "-std=c++17", r"C:\Repo\Src\A.cpp"],
                    "directory": r"C:\Repo\build",
                    "file": r"C:\Repo\Src\A.cpp",
                }
            ],
        )
        after, _ = self._database(
            "windows-suppress-after",
            [
                {
                    "arguments": [r"c:\tools\G++.EXE", "-std=c++20", r"c:\repo\src\a.cpp"],
                    "directory": r"c:\repo\build",
                    "file": r"c:\repo\src\a.cpp",
                }
            ],
        )

        report = compare_databases(
            before,
            after,
            before_project_root=r"C:\Repo",
            after_project_root=r"c:\repo",
            suppressions=["standard:SRC/*.CPP"],
        )

        self.assertTrue(report["units"][0]["suppressed"])
        self.assertEqual(report["summary"]["visible_units"], 0)

    def test_serialization_is_canonical_and_bounded(self) -> None:
        before_root = self.root / "before"
        after_root = self.root / "after"
        report = self._compare(
            [self._entry(before_root, "src/a.cpp", "-O0")],
            [self._entry(after_root, "src/a.cpp", "-O2")],
        )

        compact = dumps_diff(report)
        self.assertEqual(json.loads(compact), report)
        self.assertEqual(compact, dumps_diff(json.loads(compact)))
        self.assertTrue(dumps_diff(report, pretty=True).endswith("\n"))

    def test_cli_dispatch_exit_codes_and_dual_input_output_protection(self) -> None:
        before_root = self.root / "cli-before"
        after_root = self.root / "cli-after"
        before, _ = self._database(
            "cli-before",
            [self._entry(before_root, "src/a.cpp", "-std=c++17")],
        )
        after, _ = self._database(
            "cli-after",
            [self._entry(after_root, "src/a.cpp", "-std=c++20")],
        )
        output = self.root / "diff.json"

        result = buildscope_main(
            [
                "diff",
                str(before),
                str(after),
                "--before-project-root",
                str(before_root),
                "--after-project-root",
                str(after_root),
                "--output",
                str(output),
            ]
        )

        self.assertEqual(result, 1)
        self.assertEqual(json.loads(output.read_text(encoding="utf-8"))["summary"]["changed"], 1)

        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            self.assertEqual(
                buildscope_main(["diff", str(before), str(after), "--output", str(after)]),
                2,
            )
        self.assertIn("must not overwrite", stderr.getvalue())
        self.assertTrue(after.is_file())

        hardlink = self.root / "after-hardlink.json"
        try:
            os.link(after, hardlink)
        except OSError as error:
            self.skipTest(f"hard links are unavailable: {error}")
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(
                buildscope_main(["diff", str(before), str(after), "--output", str(hardlink)]),
                2,
            )
        self.assertEqual(os.stat(after).st_ino, os.stat(hardlink).st_ino)

    def test_cli_returns_zero_when_every_change_is_suppressed(self) -> None:
        before_root = self.root / "suppressed-before"
        after_root = self.root / "suppressed-after"
        before, _ = self._database(
            "suppressed-before",
            [self._entry(before_root, "src/a.cpp", "-std=c++17")],
        )
        after, _ = self._database(
            "suppressed-after",
            [self._entry(after_root, "src/a.cpp", "-std=c++20")],
        )
        stdout = io.StringIO()

        with contextlib.redirect_stdout(stdout):
            result = buildscope_main(
                [
                    "diff",
                    str(before),
                    str(after),
                    "--before-project-root",
                    str(before_root),
                    "--after-project-root",
                    str(after_root),
                    "--suppress",
                    "standard:src/a.cpp",
                ]
            )

        self.assertEqual(result, 0)
        self.assertEqual(json.loads(stdout.getvalue())["summary"]["visible_units"], 0)


if __name__ == "__main__":
    unittest.main()
