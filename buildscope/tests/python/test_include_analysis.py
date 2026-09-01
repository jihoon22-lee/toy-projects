from __future__ import annotations

import json
import os
import shutil
import stat
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

import buildscope.compiler_replay as compiler_replay
import buildscope.include_analysis as include_analysis
from buildscope.include_analysis import IncludeAnalysisError
from buildscope.snapshot import load_compilation_database


def _entry(source: Path, arguments: list[str], *, directory: Path) -> dict:
    return {
        "directory": str(directory),
        "file": str(source),
        "normalized": {
            "argv": arguments,
            "command_style": "posix",
            "compiler": {"name": Path(arguments[0]).name, "path": arguments[0]},
            "include_paths": [],
        },
    }


def _load_entry(project: Path, source: Path, arguments: list[str], *, directory: Path) -> dict:
    database = directory / "compile_commands.json"
    database.write_text(
        json.dumps(
            [
                {
                    "directory": str(directory),
                    "file": str(source),
                    "arguments": arguments,
                }
            ]
        ),
        encoding="utf-8",
    )
    return load_compilation_database(database, project_root=project)["entries"][0]


def _system_compilers() -> list[str]:
    return [name for name in ("g++", "clang++") if shutil.which(name, path=os.defpath) is not None]


class IncludeAnalysisTests(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.tmp_path = Path(self._temporary.name)

    def tearDown(self) -> None:
        self._temporary.cleanup()

    def test_sanitizer_drops_compile_output_and_dependency_flags_but_keeps_safe_flags(
        self,
    ) -> None:
        project = self.tmp_path / "project"
        build = project / "build"
        source = project / "src" / "main.cpp"
        include = project / "include"
        system = project / "system"
        source.parent.mkdir(parents=True)
        build.mkdir()
        include.mkdir()
        system.mkdir()
        source.write_text("int main;\n", encoding="utf-8")
        arguments = [
            "g++",
            "-c",
            "-std=c++20",
            "-DDEBUG",
            "-D",
            "FEATURE=1",
            "-I",
            str(include),
            "-isystem",
            str(system),
            "-M",
            "-MM",
            "-MD",
            "-MMD",
            "-MP",
            "-MG",
            "-MF",
            "dependencies.d",
            "-MT",
            "main.o",
            "-MQ",
            "main.o",
            "-MJ",
            "compile_commands.d",
            "-MFjoined.d",
            "-MTjoined.o",
            "-MQjoined.o",
            "-MJjoined.json",
            "--dependency-file=long-form.d",
            "-o",
            "main.o",
            str(source),
        ]

        sanitized = compiler_replay.sanitized_arguments(
            arguments, cwd=build, source=source.resolve()
        )

        self.assertEqual(
            sanitized,
            [
                "-std=c++20",
                "-DDEBUG",
                "-D",
                "FEATURE=1",
                "-I",
                str(include),
                "-isystem",
                str(system),
            ],
        )

    def test_sanitizer_rejects_plugin_argument(self) -> None:
        project = self.tmp_path / "project"
        build = project / "build"
        source = project / "src" / "main.cpp"
        source.parent.mkdir(parents=True)
        build.mkdir()
        source.write_text("int main;\n", encoding="utf-8")

        with self.assertRaisesRegex(IncludeAnalysisError, "unsafe compiler option"):
            compiler_replay.sanitized_arguments(
                ["g++", "-fplugin=forbidden.so", str(source)],
                cwd=build,
                source=source.resolve(),
            )

    def test_sanitizer_rejects_response_file_argument(self) -> None:
        project = self.tmp_path / "project"
        build = project / "build"
        source = project / "src" / "main.cpp"
        source.parent.mkdir(parents=True)
        build.mkdir()
        source.write_text("int main;\n", encoding="utf-8")

        with self.assertRaisesRegex(IncludeAnalysisError, "response files"):
            compiler_replay.sanitized_arguments(
                ["g++", "@response-file.rsp", str(source)],
                cwd=build,
                source=source.resolve(),
            )

    def test_sanitizer_rejects_an_extra_input_operand(self) -> None:
        project = self.tmp_path / "project"
        build = project / "build"
        source = project / "src" / "main.cpp"
        extra = project / "src" / "other.cpp"
        source.parent.mkdir(parents=True)
        build.mkdir()
        source.write_text("int main;\n", encoding="utf-8")
        extra.write_text("int other;\n", encoding="utf-8")

        with self.assertRaisesRegex(IncludeAnalysisError, "extra input operand"):
            compiler_replay.sanitized_arguments(
                ["g++", str(source), str(extra)],
                cwd=build,
                source=source.resolve(),
            )

    def test_build_trace_command_rejects_a_compiler_inside_the_project(self) -> None:
        project = self.tmp_path / "project"
        build = project / "build"
        source = project / "src" / "main.cpp"
        project_compiler = project / "tools" / "g++"
        source.parent.mkdir(parents=True)
        build.mkdir()
        project_compiler.parent.mkdir()
        source.write_text("int main;\n", encoding="utf-8")
        project_compiler.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
        project_compiler.chmod(project_compiler.stat().st_mode | stat.S_IXUSR)

        entry = _entry(source, ["g++", str(source)], directory=build)

        with (
            mock.patch.object(compiler_replay.shutil, "which", return_value=str(project_compiler)),
            self.assertRaisesRegex(IncludeAnalysisError, "approved system executable"),
        ):
            compiler_replay.build_trace_command(entry, project)

    def test_estimate_entry_marks_non_explicit_system_header_unresolved(self) -> None:
        project = self.tmp_path / "project"
        build = project / "build"
        source = project / "src" / "main.cpp"
        source.parent.mkdir(parents=True)
        build.mkdir()
        source.write_text("#include <stdio.h>\nint main;\n", encoding="utf-8")

        entry = _load_entry(project, source, ["g++", "-c", str(source)], directory=build)

        with mock.patch.object(
            compiler_replay, "run_trace", side_effect=AssertionError("estimate ran compiler")
        ):
            analysis = include_analysis.estimate_entry(entry, project)
        edge = next(item for item in analysis["edges"] if item["requested"] == "stdio.h")

        self.assertEqual(analysis["evidence"], "estimated")
        self.assertEqual(analysis["command"], [])
        self.assertEqual(analysis["duration_ms"], 0)
        self.assertEqual(edge["evidence"], "estimated")
        self.assertEqual(edge["location_evidence"], "source-scan")
        self.assertIsNone(edge["resolved"])
        self.assertEqual(edge["classification"], "unresolved")
        self.assertNotEqual(edge["classification"], "system")

    def test_compiler_measured_classifies_generated_and_external_system_headers(self) -> None:
        compilers = _system_compilers()
        if not compilers:
            self.skipTest("requires g++ or clang++")

        for compiler in compilers:
            with self.subTest(compiler=compiler):
                tag = compiler.replace("+", "p")
                project = self.tmp_path / f"project-classification-{tag}"
                build = project / "build"
                source = project / "src" / "main.cpp"
                external = self.tmp_path / f"external-{tag}" / "include"
                source.parent.mkdir(parents=True)
                build.mkdir()
                external.mkdir(parents=True)
                source.write_text(
                    '#include "generated.hpp"\n#include <external.hpp>\n'
                    "int values = GENERATED + EXTERNAL;\n",
                    encoding="utf-8",
                )
                (build / "generated.hpp").write_text("#define GENERATED 1\n", encoding="utf-8")
                (external / "external.hpp").write_text("#define EXTERNAL 2\n", encoding="utf-8")

                entry = _load_entry(
                    project,
                    source,
                    [
                        compiler,
                        "-I",
                        str(build),
                        "-isystem",
                        str(external),
                        "-c",
                        str(source),
                    ],
                    directory=build,
                )

                analysis = include_analysis.analyze_entry(entry, project)
                generated = next(
                    item for item in analysis["edges"] if item["requested"] == "generated.hpp"
                )
                system = next(
                    item for item in analysis["edges"] if item["requested"] == "external.hpp"
                )

                self.assertEqual(generated["resolved"], "build/generated.hpp")
                self.assertEqual(generated["classification"], "generated")
                self.assertEqual(generated["evidence"], "compiler-measured")
                self.assertEqual(system["resolved"], external.joinpath("external.hpp").as_posix())
                self.assertEqual(system["classification"], "system")
                self.assertEqual(system["evidence"], "compiler-measured")

    def test_wrapper_argv_is_stripped_and_direct_normalized_compiler_is_used(self) -> None:
        compilers = _system_compilers()
        if not compilers:
            self.skipTest("requires g++ or clang++")
        compiler = compilers[0]
        project = self.tmp_path / "project"
        build = project / "build"
        source = project / "src" / "main.cpp"
        source.parent.mkdir(parents=True)
        build.mkdir()
        source.write_text("int wrapped;\n", encoding="utf-8")
        entry = _load_entry(
            project,
            source,
            [
                "env",
                "BUILDSCOPE_WRAPPER_SENTINEL=must-not-run",
                "ccache",
                compiler,
                "-std=c++17",
                "-c",
                str(source),
                "-o",
                "wrapped.o",
            ],
            directory=build,
        )

        with mock.patch.object(include_analysis, "run_trace", return_value=(0, "", 0)) as run_trace:
            analysis = include_analysis.analyze_entry(entry, project)

        run_trace.assert_called_once()
        command = run_trace.call_args.args[0]
        resolved_compiler = Path(shutil.which(compiler, path=os.defpath)).resolve()
        self.assertEqual(Path(command[0]), resolved_compiler)
        self.assertNotIn("env", command)
        self.assertNotIn("ccache", command)
        self.assertNotIn("BUILDSCOPE_WRAPPER_SENTINEL=must-not-run", command)
        self.assertEqual(command[-1], str(source.resolve()))
        self.assertEqual(analysis["evidence"], "compiler-measured")

    def test_run_trace_times_out_and_stops_real_child_promptly(self) -> None:
        command = [sys.executable, "-c", "import time; time.sleep(10)"]
        started = time.monotonic()

        with (
            mock.patch.object(compiler_replay, "TRACE_TIMEOUT_SECONDS", 0.05),
            self.assertRaisesRegex(IncludeAnalysisError, "timed out"),
        ):
            compiler_replay.run_trace(command, self.tmp_path)

        self.assertLess(time.monotonic() - started, 5.0)

    def test_run_trace_rejects_oversized_stderr_from_real_child_promptly(self) -> None:
        command = [
            sys.executable,
            "-c",
            "import sys; sys.stderr.write('x' * 4096); sys.stderr.flush()",
        ]
        started = time.monotonic()

        with (
            mock.patch.object(compiler_replay, "MAX_TRACE_BYTES", 64),
            self.assertRaisesRegex(IncludeAnalysisError, "output limit"),
        ):
            compiler_replay.run_trace(command, self.tmp_path)

        self.assertLess(time.monotonic() - started, 5.0)

    def test_compiler_measured_trace_selects_first_same_basename_and_records_alternative(
        self,
    ) -> None:
        compilers = _system_compilers()
        if not compilers:
            self.skipTest("requires g++ or clang++")

        for compiler in compilers:
            with self.subTest(compiler=compiler):
                tag = compiler.replace("+", "p")
                project = self.tmp_path / f"project-same-{tag}"
                build = project / "build"
                source = project / "src" / "main.cpp"
                first = project / "first"
                second = project / "second"
                source.parent.mkdir(parents=True)
                build.mkdir()
                first.mkdir()
                second.mkdir()
                source.write_text(
                    "#include <same.hpp>\nint selected = SELECTED;\n", encoding="utf-8"
                )
                (first / "same.hpp").write_text("#define SELECTED 1\n", encoding="utf-8")
                (second / "same.hpp").write_text("#define SELECTED 2\n", encoding="utf-8")

                entry = _load_entry(
                    project,
                    source,
                    [
                        compiler,
                        "-std=c++17",
                        "-I",
                        str(first),
                        "-I",
                        str(second),
                        "-c",
                        str(source),
                        "-o",
                        "main.o",
                    ],
                    directory=build,
                )

                analysis = include_analysis.analyze_entry(entry, project)
                edge = next(item for item in analysis["edges"] if item["requested"] == "same.hpp")

                self.assertEqual(analysis["evidence"], "compiler-measured")
                self.assertEqual(edge["parent"], "src/main.cpp")
                self.assertEqual(edge["line"], 1)
                self.assertEqual(edge["delimiter"], "angle")
                self.assertEqual(edge["resolved"], "first/same.hpp")
                self.assertEqual(edge["alternatives"], ["second/same.hpp"])
                self.assertEqual(
                    [
                        (item["candidate"], item["order"], item["selected"])
                        for item in edge["search"]
                    ],
                    [
                        ("first/same.hpp", 0, True),
                        ("second/same.hpp", 1, False),
                    ],
                )

    def test_compiler_measured_trace_records_missing_header(self) -> None:
        compilers = _system_compilers()
        if not compilers:
            self.skipTest("requires g++ or clang++")
        compiler = compilers[0]

        project = self.tmp_path / "project"
        build = project / "build"
        source = project / "src" / "main.cpp"
        source.parent.mkdir(parents=True)
        build.mkdir()
        source.write_text('#include "missing.hpp"\nint missing = 0;\n', encoding="utf-8")

        entry = _load_entry(project, source, [compiler, "-c", str(source)], directory=build)
        analysis = include_analysis.analyze_entry(entry, project)
        edge = next(item for item in analysis["edges"] if item["requested"] == "missing.hpp")

        self.assertEqual(edge["parent"], "src/main.cpp")
        self.assertEqual(edge["line"], 1)
        self.assertIsNone(edge["resolved"])
        self.assertEqual(edge["classification"], "missing")
        self.assertEqual(edge["evidence"], "compiler-measured")
        self.assertIn(
            "compiler-trace-failed",
            {item["code"] for item in analysis["diagnostics"]},
        )
        self.assertEqual(edge["location_evidence"], "compiler-diagnostic")

    def test_search_selects_a_duplicate_resolved_path_only_once(self) -> None:
        project = self.tmp_path / "project"
        parent = project / "include" / "main.hpp"
        selected = project / "include" / "common.hpp"
        parent.parent.mkdir(parents=True)
        parent.write_text('#include "common.hpp"\n', encoding="utf-8")
        selected.write_text("#pragma once\n", encoding="utf-8")

        search, alternatives = include_analysis._search_records(
            parent,
            "common.hpp",
            "quote",
            [("quote", parent.parent), ("include", parent.parent)],
            selected,
            project,
        )

        self.assertEqual(sum(item["selected"] for item in search), 1)
        self.assertEqual(alternatives, [])
        self.assertEqual([item["candidate"] for item in search], ["include/common.hpp"] * 3)

    def test_snapshot_annotation_stops_at_the_unit_limit(self) -> None:
        snapshot = {"entries": [{"id": 1}, {"id": 2}]}
        estimated = {
            "command": [],
            "diagnostics": [],
            "duration_ms": 0,
            "edges": [],
            "evidence": "estimated",
        }
        with mock.patch.object(include_analysis, "estimate_entry", return_value=estimated) as run:
            result = include_analysis.annotate_snapshot(
                snapshot,
                self.tmp_path,
                mode="estimate",
                max_units=1,
                budget_seconds=1,
            )

        run.assert_called_once()
        self.assertEqual(result["schema_version"], "buildscope.snapshot/v3")
        self.assertEqual(result["entries"][0]["include_analysis"]["evidence"], "estimated")
        limited = result["entries"][1]["include_analysis"]
        self.assertEqual(limited["evidence"], "unavailable")
        self.assertEqual(limited["diagnostics"][0]["code"], "include-analysis-unit-limit")
