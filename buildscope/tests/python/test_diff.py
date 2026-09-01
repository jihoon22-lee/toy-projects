from __future__ import annotations

import contextlib
import io
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from buildscope.__main__ import main as buildscope_main
from buildscope.diff import DIFF_SCHEMA_VERSION, DiffError, compare_databases, dumps_diff
from buildscope.diff_policy import (
    DiffPolicyError,
    matching_suppression,
    parse_suppressions,
)


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

    def test_absolute_source_operands_and_path_flags_rebase_with_project(self) -> None:
        before_root = self.root / "before"
        after_root = self.root / "after"
        before = {
            "arguments": [
                str(before_root / "tools" / "g++"),
                "-include",
                str(before_root / "generated" / "config.h"),
                "-c",
                str(before_root / "src" / "a.cpp"),
            ],
            "directory": str(before_root),
            "file": "src/a.cpp",
        }
        after = {
            "arguments": [
                str(after_root / "tools" / "g++"),
                "-include",
                str(after_root / "generated" / "config.h"),
                "-c",
                str(after_root / "src" / "a.cpp"),
            ],
            "directory": str(after_root),
            "file": "src/a.cpp",
        }

        report = self._compare([before], [after])

        self.assertEqual(report["units"], [])
        self.assertEqual(report["summary"]["unchanged"], 1)

    def test_path_bearing_residual_options_rebase_without_hiding_drift(self) -> None:
        before_root = self.root / "before"
        after_root = self.root / "after"

        def flags(root: Path) -> tuple[str, ...]:
            return (
                "-MF",
                str(root / "build" / "deps.d"),
                "-MT",
                "object-target",
                "-MQ",
                "quoted-target",
                "-include-pch",
                str(root / "generated" / "prefix.pch"),
                "-imacros",
                str(root / "generated" / "macros.h"),
                f"--gcc-toolchain={root / 'toolchain'}",
                f"-resource-dir={root / 'resources'}",
                f"-fsanitize-ignorelist={root / 'config' / 'ignore.txt'}",
                f"-B{root / 'bin'}",
                f"/FI{root / 'generated' / 'forced.h'}",
            )

        report = self._compare(
            [self._entry(before_root, "src/a.cpp", *flags(before_root))],
            [self._entry(after_root, "src/a.cpp", *flags(after_root))],
        )

        self.assertEqual(report["units"], [])
        self.assertEqual(report["summary"]["unchanged"], 1)

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

    def test_versioned_and_target_prefixed_compilers_keep_their_family(self) -> None:
        before_root = self.root / "before"
        after_root = self.root / "after"
        report = self._compare(
            [self._entry(before_root, "src/a.cpp", compiler="x86_64-linux-gnu-g++-12")],
            [self._entry(after_root, "src/a.cpp", compiler="clang++-18")],
        )

        change = report["units"][0]["changes"][0]
        self.assertEqual(change["category"], "compiler")
        self.assertEqual(change["before"]["family"], "gcc")
        self.assertEqual(change["after"]["family"], "clang")

    def test_external_compiler_path_change_is_reported(self) -> None:
        before_root = self.root / "before"
        after_root = self.root / "after"

        report = self._compare(
            [self._entry(before_root, "src/a.cpp", compiler="/opt/toolchain-v1/bin/g++")],
            [self._entry(after_root, "src/a.cpp", compiler="/opt/toolchain-v2/bin/g++")],
        )

        compiler = report["units"][0]["changes"][0]
        self.assertEqual(compiler["category"], "compiler")
        self.assertEqual(compiler["before"]["path"], "/opt/toolchain-v1/bin/g++")
        self.assertEqual(compiler["after"]["path"], "/opt/toolchain-v2/bin/g++")

    def test_separated_standard_spelling_is_normalized(self) -> None:
        before_root = self.root / "before"
        after_root = self.root / "after"

        report = self._compare(
            [self._entry(before_root, "src/a.cpp", "-std", "c++17")],
            [self._entry(after_root, "src/a.cpp")],
        )

        self.assertEqual(report["units"][0]["changes"][0]["category"], "standard")
        self.assertEqual(report["units"][0]["changes"][0]["before"], "c++17")

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

    def test_directory_move_with_configuration_drift_retains_both_changes(self) -> None:
        before_root = self.root / "before"
        after_root = self.root / "after"

        report = self._compare(
            [self._entry(before_root, "src/a.cpp", "-std=c++17")],
            [self._entry(after_root, "renamed/a.cpp", "-std=c++20")],
        )

        self.assertEqual(report["summary"]["moved"], 1)
        self.assertEqual(report["summary"]["added"], 0)
        self.assertEqual(report["summary"]["removed"], 0)
        self.assertEqual(
            [change["category"] for change in report["units"][0]["changes"]],
            ["moved", "standard"],
        )

    def test_identical_configuration_does_not_invent_cross_basename_move(self) -> None:
        before_root = self.root / "before"
        after_root = self.root / "after"

        report = self._compare(
            [self._entry(before_root, "src/old.cpp", "-std=c++20")],
            [self._entry(after_root, "src/new.cpp", "-std=c++20")],
        )

        self.assertEqual(report["summary"]["moved"], 0)
        self.assertEqual(report["summary"]["added"], 1)
        self.assertEqual(report["summary"]["removed"], 1)

    def test_ambiguous_moves_remain_conservative_added_and_removed(self) -> None:
        before_root = self.root / "before"
        after_root = self.root / "after"
        before = [
            self._entry(before_root, "old/one/a.cpp", "-std=c++20"),
            self._entry(before_root, "old/two/a.cpp", "-std=c++20"),
        ]
        after = [
            self._entry(after_root, "new/one/a.cpp", "-std=c++20"),
            self._entry(after_root, "new/two/a.cpp", "-std=c++20"),
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

    def test_ambiguous_same_source_configurations_emit_diagnostic(self) -> None:
        before_root = self.root / "before"
        after_root = self.root / "after"
        before = [
            self._entry(before_root, "src/a.cpp", "-O0"),
            self._entry(before_root, "src/a.cpp", "-O1"),
        ]
        after = [
            self._entry(after_root, "src/a.cpp", "-O2"),
            self._entry(after_root, "src/a.cpp", "-O3"),
        ]

        report = self._compare(before, after)

        self.assertEqual(report["summary"]["added"], 2)
        self.assertEqual(report["summary"]["removed"], 2)
        self.assertEqual(
            report["diagnostics"][0]["code"],
            "ambiguous-configuration-match",
        )

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

    def test_suppression_star_does_not_cross_path_segments(self) -> None:
        before_root = self.root / "before"
        after_root = self.root / "after"
        report = self._compare(
            [self._entry(before_root, "src/nested/a.cpp", "-std=c++17")],
            [self._entry(after_root, "src/nested/a.cpp", "-std=c++20")],
            suppressions=["standard:src/*.cpp"],
        )

        self.assertFalse(report["units"][0]["suppressed"])
        self.assertEqual(report["summary"]["visible_units"], 1)

    def test_suppression_glob_contract_handles_basename_and_double_star(self) -> None:
        before_root = self.root / "before"
        after_root = self.root / "after"
        before = [self._entry(before_root, "src/nested/a.cpp", "-std=c++17")]
        after = [self._entry(after_root, "src/nested/a.cpp", "-std=c++20")]

        basename = self._compare(before, after, suppressions=["standard:*.cpp"])
        recursive = self._compare(before, after, suppressions=["standard:**/*.cpp"])

        self.assertTrue(basename["units"][0]["suppressed"])
        self.assertTrue(recursive["units"][0]["suppressed"])

    def test_suppression_glob_contract_covers_literals_question_and_inner_double_star(
        self,
    ) -> None:
        rules = parse_suppressions(["flag:ignored/*.c", "standard:src/**/file?.cpp", "target:a**b"])

        self.assertEqual(
            matching_suppression(
                rules,
                "standard",
                "",
                "src/nested/file1.cpp",
            ),
            "standard:src/**/file?.cpp",
        )
        self.assertEqual(
            matching_suppression(rules, "target", "axb", ""),
            "target:a**b",
        )
        self.assertEqual(matching_suppression(rules, "compiler", "src/a.cpp", ""), "")

    def test_invalid_and_duplicate_suppressions_fail_closed(self) -> None:
        with self.assertRaisesRegex(DiffPolicyError, "unknown suppression category"):
            parse_suppressions(["typo:*"])
        with self.assertRaisesRegex(DiffPolicyError, "duplicate suppression"):
            parse_suppressions(["standard:*", "standard:*"])
        with self.assertRaisesRegex(DiffPolicyError, "must not be empty"):
            parse_suppressions(["standard:"])
        with self.assertRaisesRegex(DiffPolicyError, "supports only"):
            parse_suppressions(["standard:src/[ab].cpp"])
        with self.assertRaisesRegex(DiffPolicyError, "suppression count"):
            parse_suppressions(["standard:*"] * 257)
        with self.assertRaisesRegex(DiffPolicyError, "non-empty bounded"):
            parse_suppressions(["standard:\0bad"])
        with self.assertRaisesRegex(DiffPolicyError, "non-empty bounded"):
            parse_suppressions(["standard:" + "x" * 1025])

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

    def test_malformed_or_contradictory_invocations_fail_closed(self) -> None:
        before_root = self.root / "before"
        after_root = self.root / "after"
        malformed = self._entry(before_root, "src/a.cpp")
        malformed["arguments"].append("-I")
        with self.assertRaisesRegex(DiffError, "missing-include"):
            self._compare(
                [malformed],
                [self._entry(after_root, "src/a.cpp")],
            )

        before = self._entry(before_root, "src/a.cpp", output_target="declared")
        after = self._entry(after_root, "src/a.cpp", output_target="declared")
        before["output"] = "CMakeFiles/other.dir/src/a.cpp.o"
        with self.assertRaisesRegex(DiffError, "output-mismatch"):
            self._compare([before], [after])

        for option in ("-MF", "-resource-dir="):
            malformed = self._entry(before_root, "src/a.cpp")
            malformed["arguments"].append(option)
            with self.subTest(option=option), self.assertRaises(DiffError):
                self._compare(
                    [malformed],
                    [self._entry(after_root, "src/a.cpp")],
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
        with (
            patch("buildscope.diff.MAX_SNAPSHOT_BYTES", 1),
            self.assertRaisesRegex(DiffError, "exceeds 1 byte"),
        ):
            dumps_diff(report)

    def test_changed_report_is_byte_identical_after_input_reordering(self) -> None:
        before_root = self.root / "before"
        after_root = self.root / "after"
        before = [
            self._entry(before_root, "src/a.cpp", "-O0"),
            self._entry(before_root, "src/b.cpp", "-std=c++20"),
        ]
        after = [
            self._entry(after_root, "src/a.cpp", "-O2"),
            self._entry(after_root, "src/b.cpp", "-std=c++20"),
        ]

        first = dumps_diff(self._compare(before, after))
        second = dumps_diff(self._compare(list(reversed(before)), list(reversed(after))))

        self.assertEqual(first, second)

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
