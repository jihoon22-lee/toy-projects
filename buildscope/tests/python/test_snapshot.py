from __future__ import annotations

import contextlib
import io
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from buildscope.__main__ import main
from buildscope._command import (
    CommandError,
    compiler_record,
    parse_invocation,
    split_windows_command,
)
from buildscope._io import SnapshotIoError, read_bounded_regular, write_atomic_text
from buildscope._metadata import extract_metadata, output_from_argv
from buildscope._paths import _native_record, native_mtime, path_record
from buildscope.normalize import annotate_entry_sets
from buildscope.snapshot import (
    SCHEMA_VERSION,
    SCHEMA_VERSION_V1,
    SCHEMA_VERSION_V3,
    SnapshotError,
    dumps_snapshot,
    load_compilation_database,
)


class SnapshotTests(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.root = Path(self._temporary.name)

    def tearDown(self) -> None:
        self._temporary.cleanup()

    def _write_database(self, payload: object, *, path: Path | None = None) -> Path:
        path = path or self.root / "compile_commands.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(payload), encoding="utf-8")
        return path

    def _write_raw_database(self, raw: bytes, *, path: Path | None = None) -> Path:
        path = path or self.root / "compile_commands.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(raw)
        return path

    def _project_layout(self) -> Path:
        project = self.root / "project"
        for relative in ("src", "build", "include"):
            (project / relative).mkdir(parents=True, exist_ok=True)
        return project

    def test_load_preserves_both_entry_forms_and_orders_by_source(self) -> None:
        project = self._project_layout()
        build = project / "build"
        source_a = project / "src" / "a.cpp"
        source_z = project / "src" / "z.cpp"
        source_a.write_text("int a;\n", encoding="utf-8")
        source_z.write_text("int z;\n", encoding="utf-8")
        (build / "z.o").write_text("object\n", encoding="utf-8")
        database = self._write_database(
            [
                {
                    "directory": str(build),
                    "file": str(source_z),
                    "command": f"c++ -DZED=1 -c {source_z}",
                    "output": "z.o",
                },
                {
                    "directory": str(build),
                    "file": str(source_a),
                    "arguments": ["c++", "-std=c++20", "-c", str(source_a)],
                },
            ],
            path=build / "compile_commands.json",
        )

        snapshot = load_compilation_database(database, project_root=project)

        self.assertEqual(snapshot["schema_version"], SCHEMA_VERSION)
        self.assertEqual(
            snapshot["source"],
            {
                "entry_count": 2,
                "path": str(database.resolve()),
                "project_root": str(project.resolve()),
            },
        )
        self.assertEqual(
            [entry["normalized"]["source"]["path"] for entry in snapshot["entries"]],
            ["src/a.cpp", "src/z.cpp"],
        )
        self.assertEqual(
            snapshot["entries"][0]["normalized"]["argv"],
            ["c++", "-std=c++20", "-c", str(source_a)],
        )
        self.assertEqual(snapshot["entries"][1]["command"], f"c++ -DZED=1 -c {source_z}")
        self.assertEqual(snapshot["entries"][1]["output"], "z.o")

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
                "arguments must be a bounded string array",
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

    def test_cli_can_emit_v1_compatibility_projection(self) -> None:
        database = self._write_database(
            [
                {
                    "directory": "/work",
                    "file": "/work/main.cpp",
                    "arguments": ["c++", "-c", "/work/main.cpp"],
                    "command": "c++ -c /work/main.cpp",
                }
            ]
        )
        output = self.root / "snapshot-v1.json"

        self.assertEqual(
            main(
                [
                    str(database),
                    "--schema-version",
                    "v1",
                    "--output",
                    str(output),
                ]
            ),
            0,
        )

        snapshot = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(snapshot["schema_version"], SCHEMA_VERSION_V1)
        self.assertNotIn("project_root", snapshot["source"])
        self.assertNotIn("normalized", snapshot["entries"][0])
        self.assertEqual(snapshot["entries"][0]["arguments"][0], "c++")
        self.assertIsNone(snapshot["entries"][0]["command"])

    def test_cli_emits_v3_estimated_include_explanations(self) -> None:
        project = self._project_layout()
        source = project / "src" / "main.cpp"
        header = project / "include" / "feature.hpp"
        source.write_text('#include "feature.hpp"\nint feature;\n', encoding="utf-8")
        header.write_text("#define FEATURE 1\n", encoding="utf-8")
        database = self._write_database(
            [
                {
                    "directory": str(project / "build"),
                    "file": str(source),
                    "arguments": [
                        "c++",
                        "-I",
                        str(project / "include"),
                        "-c",
                        str(source),
                    ],
                }
            ],
            path=project / "build" / "compile_commands.json",
        )
        output = self.root / "snapshot-v3.json"

        self.assertEqual(
            main(
                [
                    str(database),
                    "--project-root",
                    str(project),
                    "--include-analysis",
                    "estimate",
                    "--output",
                    str(output),
                ]
            ),
            0,
        )

        snapshot = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(snapshot["schema_version"], SCHEMA_VERSION_V3)
        analysis = snapshot["entries"][0]["include_analysis"]
        self.assertEqual(analysis["evidence"], "estimated")
        edge = analysis["edges"][0]
        self.assertEqual(edge["resolved"], "include/feature.hpp")
        self.assertEqual(edge["classification"], "project")
        self.assertEqual(edge["location_evidence"], "source-scan")

    def test_cli_rejects_include_analysis_with_a_legacy_schema(self) -> None:
        database = self._write_database(
            [{"directory": "/work", "file": "/work/main.cpp", "arguments": ["c++"]}]
        )
        output = self.root / "legacy.json"
        stderr = io.StringIO()

        with contextlib.redirect_stderr(stderr):
            result = main(
                [
                    str(database),
                    "--include-analysis",
                    "estimate",
                    "--schema-version",
                    "v2",
                    "--output",
                    str(output),
                ]
            )

        self.assertEqual(result, 2)
        self.assertFalse(output.exists())
        self.assertIn("requires --schema-version v3", stderr.getvalue())

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

    def test_posix_command_tokenization_preserves_quotes_and_shell_literals(self) -> None:
        project = self._project_layout()
        build = project / "build"
        source = project / "src" / "main file.cpp"
        source.write_text("int main_file;\n", encoding="utf-8")
        (build / "include path").mkdir()
        (build / "main file.o").write_text("object\n", encoding="utf-8")
        sentinel = self.root / "must-not-be-created"
        command = (
            "clang++ -std=gnu++20 -DNAME='hello world' "
            f"-DVALUE='$(touch {sentinel})' -I'include path' "
            f"-isystem '/usr/include/c++/12' --sysroot=/opt/sdk "
            f"--target=x86_64-linux-gnu -c '{source}' -o 'main file.o'"
        )
        database = self._write_database(
            [
                {
                    "directory": str(build),
                    "file": str(source),
                    "command": command,
                }
            ],
            path=build / "compile_commands.json",
        )

        with (
            mock.patch("os.system", side_effect=AssertionError("shell execution")),
            mock.patch("subprocess.run", side_effect=AssertionError("process execution")),
            mock.patch("subprocess.Popen", side_effect=AssertionError("process execution")),
        ):
            snapshot = load_compilation_database(database, project_root=project)

        entry = snapshot["entries"][0]
        self.assertEqual(entry["normalized"]["command_style"], "posix")
        self.assertEqual(
            entry["normalized"]["argv"],
            [
                "clang++",
                "-std=gnu++20",
                "-DNAME=hello world",
                f"-DVALUE=$(touch {sentinel})",
                "-Iinclude path",
                "-isystem",
                "/usr/include/c++/12",
                "--sysroot=/opt/sdk",
                "--target=x86_64-linux-gnu",
                "-c",
                str(source),
                "-o",
                "main file.o",
            ],
        )
        self.assertEqual(entry["normalized"]["standard"], "gnu++20")
        self.assertEqual(
            entry["normalized"]["defines"],
            [
                {"action": "define", "name": "NAME", "value": "hello world"},
                {"action": "define", "name": "VALUE", "value": f"$(touch {sentinel})"},
            ],
        )
        self.assertEqual(
            entry["normalized"]["include_paths"][0]["path"],
            "build/include path",
        )
        self.assertEqual(entry["normalized"]["include_paths"][0]["scope"], "project")
        self.assertEqual(entry["normalized"]["include_paths"][0]["kind"], "include")
        self.assertEqual(entry["normalized"]["sysroot"]["path"], "/opt/sdk")
        self.assertEqual(entry["normalized"]["target"]["triple"], "x86_64-linux-gnu")
        self.assertFalse(sentinel.exists())

    def test_arguments_take_precedence_while_both_raw_forms_are_preserved(self) -> None:
        project = self._project_layout()
        source = project / "src" / "main.cpp"
        source.write_text("int main;\n", encoding="utf-8")
        sentinel = self.root / "arguments-must-win"
        arguments = [
            "clang++",
            "-std=c++20",
            "-DORIGIN=arguments",
            "-DVALUE=argv value",
            "-c",
            str(source),
        ]
        command = f"clang++ -std='unterminated -DORIGIN=command $(touch {sentinel})"
        database = self._write_database(
            [
                {
                    "directory": str(project / "build"),
                    "file": str(source),
                    "arguments": arguments,
                    "command": command,
                }
            ]
        )

        snapshot = load_compilation_database(database, project_root=project)
        entry = snapshot["entries"][0]
        self.assertEqual(entry["arguments"], arguments)
        self.assertEqual(entry["command"], command)
        self.assertEqual(entry["normalized"]["argv"], arguments)
        self.assertEqual(entry["normalized"]["standard"], "c++20")
        self.assertEqual(
            entry["normalized"]["defines"],
            [
                {"action": "define", "name": "ORIGIN", "value": "arguments"},
                {"action": "define", "name": "VALUE", "value": "argv value"},
            ],
        )
        self.assertFalse(sentinel.exists())

    def test_windows_crt_tokenization_and_clang_cl_metadata(self) -> None:
        database = self._write_database(
            [
                {
                    "directory": r"C:\repo\build",
                    "file": r"C:\repo\src\app main.cpp",
                    "command": (
                        r'clang-cl.exe /std:c++20 /DNAME="hello world" '
                        r'/DENV="%PATH%" /I "C:\Program Files\SDK\include" '
                        r'/imsvc "C:\MSVC\include" '
                        r"/clang:-target=x86_64-pc-windows-msvc /c "
                        r'"src\app main.cpp" /Fo "build\app main.obj"'
                    ),
                }
            ]
        )

        snapshot = load_compilation_database(database)
        entry = snapshot["entries"][0]
        normalized = entry["normalized"]
        self.assertEqual(normalized["command_style"], "windows")
        self.assertEqual(
            normalized["argv"],
            [
                "clang-cl.exe",
                "/std:c++20",
                "/DNAME=hello world",
                "/DENV=%PATH%",
                "/I",
                r"C:\Program Files\SDK\include",
                "/imsvc",
                r"C:\MSVC\include",
                "/clang:-target=x86_64-pc-windows-msvc",
                "/c",
                r"src\app main.cpp",
                "/Fo",
                r"build\app main.obj",
            ],
        )
        self.assertEqual(
            normalized["compiler"],
            {
                "family": "clang-cl",
                "name": "clang-cl.exe",
                "path": "clang-cl.exe",
                "wrappers": [],
            },
        )
        self.assertEqual(normalized["standard"], "c++20")
        self.assertEqual(normalized["language"], "c++")
        self.assertEqual(
            normalized["defines"],
            [
                {"action": "define", "name": "NAME", "value": "hello world"},
                {"action": "define", "name": "ENV", "value": "%PATH%"},
            ],
        )
        self.assertEqual(
            [(item["kind"], item["path"]) for item in normalized["include_paths"]],
            [
                ("include", "C:/Program Files/SDK/include"),
                ("system", "C:/MSVC/include"),
            ],
        )
        self.assertEqual(normalized["target"]["triple"], "x86_64-pc-windows-msvc")
        self.assertEqual(normalized["source"]["path"], "C:/repo/src/app main.cpp")

    def test_foreign_windows_project_root_is_preserved_for_scope_classification(self) -> None:
        database = self._write_database(
            [
                {
                    "directory": r"C:\repo\build",
                    "file": r"C:\repo\src\app.cpp",
                    "arguments": [
                        r"C:\LLVM\bin\clang-cl.exe",
                        "/I",
                        r"C:\repo\include",
                        "/c",
                        r"C:\repo\src\app.cpp",
                    ],
                }
            ]
        )

        snapshot = load_compilation_database(database, project_root=r"C:\repo")
        normalized = snapshot["entries"][0]["normalized"]

        self.assertEqual(snapshot["source"]["project_root"], "C:/repo")
        self.assertEqual(normalized["source"]["path"], "src/app.cpp")
        self.assertEqual(normalized["source"]["scope"], "project")
        self.assertEqual(normalized["include_paths"][0]["path"], "include")
        self.assertEqual(normalized["include_paths"][0]["scope"], "project")

    def test_env_and_ccache_wrappers_are_separated_from_the_compiler(self) -> None:
        project = self._project_layout()
        source = project / "src" / "wrapped.cpp"
        source.write_text("int wrapped;\n", encoding="utf-8")
        arguments = [
            "env",
            "FOO=bar",
            "-u",
            "SECRET",
            "ccache",
            "/usr/bin/clang++",
            "-std=c++17",
            "-c",
            str(source),
        ]
        database = self._write_database(
            [
                {
                    "directory": str(project / "build"),
                    "file": str(source),
                    "arguments": arguments,
                }
            ]
        )

        normalized = load_compilation_database(database, project_root=project)["entries"][0][
            "normalized"
        ]
        self.assertEqual(normalized["argv"], arguments)
        self.assertEqual(normalized["compiler"]["family"], "clang")
        self.assertEqual(normalized["compiler"]["name"], "clang++")
        self.assertEqual(normalized["compiler"]["path"], "/usr/bin/clang++")
        self.assertEqual(normalized["compiler"]["wrappers"], ["env", "ccache"])

    def test_metadata_supports_flag_forms_and_include_kinds(self) -> None:
        project = self._project_layout()
        build = project / "build"
        source = project / "src" / "main.cpp"
        source.write_text("int main;\n", encoding="utf-8")
        quote = project / "quote"
        system = project / "system"
        sysroot = project / "sysroot"
        for directory in (quote, system, sysroot):
            directory.mkdir()
        output = build / "CMakeFiles" / "app.dir" / "src" / "main.cpp.o"
        output.parent.mkdir(parents=True)
        output.write_text("object\n", encoding="utf-8")
        arguments = [
            "clang++",
            "-x",
            "c++",
            "-std=c++20",
            "-DDEBUG",
            "-D",
            "NAME=value",
            "-UOLD",
            "-I",
            str(project / "include"),
            "-iquote",
            str(quote),
            "-isystem",
            str(system),
            "/imsvc",
            str(project / "include"),
            "--sysroot",
            str(sysroot),
            "--target",
            "x86_64-linux-gnu",
            "-c",
            str(source),
            "-o",
            "CMakeFiles/app.dir/src/main.cpp.o",
        ]
        database = self._write_database(
            [
                {
                    "directory": str(build),
                    "file": str(source),
                    "arguments": arguments,
                }
            ],
            path=build / "compile_commands.json",
        )

        entry = load_compilation_database(database, project_root=project)["entries"][0]
        normalized = entry["normalized"]
        self.assertEqual(normalized["argv"], arguments)
        self.assertEqual(normalized["language"], "c++")
        self.assertEqual(normalized["standard"], "c++20")
        self.assertEqual(
            normalized["defines"],
            [
                {"action": "define", "name": "DEBUG", "value": None},
                {"action": "define", "name": "NAME", "value": "value"},
                {"action": "undefine", "name": "OLD", "value": None},
            ],
        )
        self.assertEqual(
            [item["kind"] for item in normalized["include_paths"]],
            ["include", "quote", "system", "system"],
        )
        self.assertEqual(normalized["sysroot"]["path"], "sysroot")
        self.assertEqual(normalized["target"]["triple"], "x86_64-linux-gnu")
        self.assertEqual(normalized["target"]["build_target"], "app")
        self.assertEqual(normalized["output"]["path"], "build/CMakeFiles/app.dir/src/main.cpp.o")
        self.assertEqual(entry["state"]["source_status"], "present")

    def test_native_project_vendor_system_and_symlink_escape_are_classified(self) -> None:
        project = self._project_layout()
        source = project / "src" / "main.cpp"
        source.write_text("int main;\n", encoding="utf-8")
        vendor = project / "third_party" / "zlib" / "include"
        vendor.mkdir(parents=True)
        system = self.root / "system" / "include"
        system.mkdir(parents=True)
        link = project / "include-link"
        link.symlink_to(system, target_is_directory=True)
        escaped_source = self.root / "outside.cpp"
        escaped_source.write_text("int outside;\n", encoding="utf-8")
        escaped_link = project / "src" / "escaped.cpp"
        escaped_link.symlink_to(escaped_source)
        arguments = [
            "clang++",
            "-I",
            str(project / "include"),
            "-I",
            str(vendor),
            "-isystem",
            str(system),
            "-I",
            str(link),
            "-c",
            str(escaped_link),
        ]
        database = self._write_database(
            [
                {
                    "directory": str(project / "build"),
                    "file": str(escaped_link),
                    "arguments": arguments,
                }
            ],
            path=project / "build" / "compile_commands.json",
        )

        normalized = load_compilation_database(database, project_root=project)["entries"][0][
            "normalized"
        ]
        self.assertEqual(normalized["source"]["scope"], "system")
        self.assertEqual(normalized["source"]["path"], str(escaped_source.resolve()))
        self.assertEqual(
            [(item["path"], item["scope"]) for item in normalized["include_paths"]],
            [
                ("include", "project"),
                ("third_party/zlib/include", "vendor"),
                (str(system.resolve()), "system"),
                (str(system.resolve()), "system"),
            ],
        )

    def test_present_missing_and_stale_sources_are_preserved_and_diagnosed(self) -> None:
        project = self._project_layout()
        build = project / "build"
        present = project / "src" / "present.cpp"
        stale = project / "src" / "stale.cpp"
        missing = project / "src" / "missing.cpp"
        present.write_text("int present;\n", encoding="utf-8")
        stale.write_text("int stale;\n", encoding="utf-8")
        present_output = build / "present.o"
        stale_output = build / "stale.o"
        present_output.write_text("present object\n", encoding="utf-8")
        stale_output.write_text("stale object\n", encoding="utf-8")
        os.utime(present, (100, 100))
        os.utime(present_output, (200, 200))
        os.utime(stale, (300, 300))
        os.utime(stale_output, (200, 200))
        payload = [
            {
                "directory": str(build),
                "file": str(present),
                "arguments": ["clang++", "-c", str(present), "-o", "present.o"],
                "output": "present.o",
            },
            {
                "directory": str(build),
                "file": str(stale),
                "arguments": ["clang++", "-c", str(stale), "-o", "stale.o"],
                "output": "stale.o",
            },
            {
                "directory": str(build),
                "file": str(missing),
                "arguments": ["clang++", "-c", str(missing)],
            },
        ]
        database = self._write_database(payload, path=build / "compile_commands.json")

        entries = load_compilation_database(database, project_root=project)["entries"]
        by_source = {entry["normalized"]["source"]["path"]: entry for entry in entries}
        self.assertEqual(by_source["src/present.cpp"]["state"]["source_status"], "present")
        self.assertEqual(by_source["src/stale.cpp"]["state"]["source_status"], "stale")
        self.assertEqual(by_source["src/missing.cpp"]["state"]["source_status"], "missing")
        self.assertEqual(by_source["src/present.cpp"]["diagnostics"], [])
        self.assertIn(
            "stale-output",
            {item["code"] for item in by_source["src/stale.cpp"]["diagnostics"]},
        )
        self.assertIn(
            "missing-source",
            {item["code"] for item in by_source["src/missing.cpp"]["diagnostics"]},
        )

    def test_duplicates_and_multiple_configurations_are_preserved(self) -> None:
        project = self._project_layout()
        source = project / "src" / "app.cpp"
        source.write_text("int app;\n", encoding="utf-8")
        debug_output = project / "build" / "debug.o"
        release_output = project / "build" / "release.o"
        debug_output.write_text("debug object\n", encoding="utf-8")
        release_output.write_text("release object\n", encoding="utf-8")
        debug = {
            "directory": str(project / "build"),
            "file": str(source),
            "arguments": ["clang++", "-DDEBUG", "-O0", "-c", str(source), "-o", "debug.o"],
            "output": "debug.o",
        }
        release = {
            "directory": str(project / "build"),
            "file": str(source),
            "arguments": ["clang++", "-DNDEBUG", "-O2", "-c", str(source), "-o", "release.o"],
            "output": "release.o",
        }
        database = self._write_database([debug, dict(debug), release])

        entries = load_compilation_database(database, project_root=project)["entries"]
        self.assertEqual(len(entries), 3)
        debug_entries = [
            entry
            for entry in entries
            if entry["normalized"]["defines"]
            == [{"action": "define", "name": "DEBUG", "value": None}]
        ]
        release_entries = [
            entry
            for entry in entries
            if entry["normalized"]["defines"]
            == [{"action": "define", "name": "NDEBUG", "value": None}]
        ]
        self.assertEqual(len(debug_entries), 2)
        self.assertTrue(all(entry["state"]["duplicate"] for entry in debug_entries))
        self.assertTrue(
            all(
                "duplicate-entry" in {item["code"] for item in entry["diagnostics"]}
                for entry in debug_entries
            )
        )
        self.assertEqual(len(release_entries), 1)
        self.assertFalse(release_entries[0]["state"]["duplicate"])
        self.assertEqual(debug_entries[0]["state"]["source_configuration_count"], 2)
        self.assertEqual(release_entries[0]["state"]["source_configuration_count"], 2)
        self.assertNotEqual(
            debug_entries[0]["normalized"]["configuration"],
            release_entries[0]["normalized"]["configuration"],
        )

    def test_duplicate_identity_is_scoped_to_each_source(self) -> None:
        project = self._project_layout()
        sources = [project / "src" / "a.cpp", project / "src" / "b.cpp"]
        for source in sources:
            source.write_text("int value;\n", encoding="utf-8")
        shared_argv = ["clang++", "-DVALUE=1", "-c"]
        database = self._write_database(
            [
                {
                    "directory": str(project / "build"),
                    "file": str(source),
                    "arguments": shared_argv,
                }
                for source in sources
            ]
        )

        entries = load_compilation_database(database, project_root=project)["entries"]

        self.assertEqual(
            entries[0]["normalized"]["configuration"],
            entries[1]["normalized"]["configuration"],
        )
        self.assertTrue(all(not entry["state"]["duplicate"] for entry in entries))
        self.assertTrue(all(entry["state"]["source_configuration_count"] == 1 for entry in entries))

    def test_repeated_entries_have_deterministic_serialization(self) -> None:
        project = self._project_layout()
        source_a = project / "src" / "a.cpp"
        source_b = project / "src" / "b.cpp"
        source_a.write_text("int a;\n", encoding="utf-8")
        source_b.write_text("int b;\n", encoding="utf-8")
        payload = [
            {
                "directory": str(project / "build"),
                "file": str(source_b),
                "arguments": ["clang++", "-c", str(source_b)],
            },
            {
                "directory": str(project / "build"),
                "file": str(source_a),
                "arguments": ["clang++", "-c", str(source_a)],
            },
        ]
        database = self._write_database(payload)
        first = dumps_snapshot(load_compilation_database(database, project_root=project))
        second = dumps_snapshot(load_compilation_database(database, project_root=project))
        self.assertEqual(first, second)

    def test_entry_sets_keep_posix_and_windows_source_keys_separate(self) -> None:
        entries = [
            {
                "normalized": {
                    "command_style": style,
                    "configuration": "sha256:" + "a" * 64,
                    "source": {"path": "shared/source.cpp"},
                },
                "state": {"duplicate": False, "source_configuration_count": 1},
                "diagnostics": [],
            }
            for style in ("posix", "windows")
        ]

        annotate_entry_sets(entries)

        self.assertTrue(all(not entry["state"]["duplicate"] for entry in entries))
        self.assertTrue(all(entry["state"]["source_configuration_count"] == 1 for entry in entries))

    def test_native_path_separator_normalization_and_windows_escape(self) -> None:
        project = self._project_layout()
        source = project / "src" / "relative.cpp"
        source.write_text("int relative;\n", encoding="utf-8")
        database = self._write_database(
            [
                {
                    "directory": str(project / "build"),
                    "file": "../src/relative.cpp",
                    "arguments": ["clang++", "-c", "../src/relative.cpp"],
                }
            ],
            path=project / "build" / "compile_commands.json",
        )
        normalized = load_compilation_database(database, project_root=project)["entries"][0][
            "normalized"
        ]
        self.assertEqual(normalized["source"]["path"], "src/relative.cpp")
        self.assertEqual(normalized["source"]["scope"], "project")

        windows_database = self._write_database(
            [
                {
                    "directory": r"C:\repo\build",
                    "file": r"C:\repo\..\outside.cpp",
                    "command": r'cl.exe /c "C:\repo\..\outside.cpp"',
                },
                {
                    "directory": r"C:\repo\build",
                    "file": r"\\server\share\src.cpp",
                    "command": r'cl.exe /c "\\server\share\src.cpp"',
                },
            ],
            path=self.root / "windows-compile_commands.json",
        )
        windows_entries = load_compilation_database(windows_database)["entries"]
        windows_sources = {
            entry["file"]: entry["normalized"]["source"] for entry in windows_entries
        }
        self.assertEqual(windows_sources[r"C:\repo\..\outside.cpp"]["path"], "C:/outside.cpp")
        self.assertEqual(windows_sources[r"C:\repo\..\outside.cpp"]["scope"], "system")
        self.assertEqual(
            windows_sources[r"\\server\share\src.cpp"]["path"],
            "//server/share/src.cpp",
        )
        self.assertEqual(windows_sources[r"\\server\share\src.cpp"]["scope"], "system")

    def test_duplicate_json_keys_nan_and_malformed_commands_are_rejected(self) -> None:
        duplicate = self._write_raw_database(
            b'[{"directory":"/work","file":"a.cpp","file":"b.cpp","arguments":["clang++"]}]'
        )
        with self.assertRaisesRegex(SnapshotError, "duplicate JSON object key: file"):
            load_compilation_database(duplicate)

        nan = self._write_raw_database(
            b'[{"directory":"/work","file":"a.cpp","arguments":["clang++"],"output":NaN}]'
        )
        with self.assertRaisesRegex(SnapshotError, "non-standard JSON constant"):
            load_compilation_database(nan)

        project = self._project_layout()
        source = project / "src" / "malformed.cpp"
        source.write_text("int malformed;\n", encoding="utf-8")
        malformed = self._write_database(
            [
                {
                    "directory": str(project / "build"),
                    "file": str(source),
                    "command": "clang++ -DNAME='unterminated -c malformed.cpp",
                }
            ]
        )
        with self.assertRaisesRegex(SnapshotError, "could not be parsed"):
            load_compilation_database(malformed, project_root=project)

        malformed_windows = self._write_database(
            [
                {
                    "directory": r"C:\repo\build",
                    "file": r"C:\repo\src\malformed.cpp",
                    "command": r'cl.exe /c "C:\repo\src\malformed.cpp',
                }
            ],
            path=self.root / "malformed-windows.json",
        )
        with self.assertRaisesRegex(SnapshotError, "unclosed Windows quote"):
            load_compilation_database(malformed_windows)

    def test_huge_argv_and_command_are_rejected_before_normalization(self) -> None:
        project = self._project_layout()
        source = project / "src" / "huge.cpp"
        source.write_text("int huge;\n", encoding="utf-8")
        argv_database = self._write_database(
            [
                {
                    "directory": str(project / "build"),
                    "file": str(source),
                    "arguments": ["clang++", "-c", str(source)],
                }
            ]
        )
        with (
            mock.patch("buildscope._command.MAX_ARGUMENTS", 2),
            self.assertRaisesRegex(SnapshotError, "bounded argv contract"),
        ):
            load_compilation_database(argv_database, project_root=project)

        command_database = self._write_database(
            [
                {
                    "directory": str(project / "build"),
                    "file": str(source),
                    "command": f"clang++ -c {source}",
                }
            ]
        )
        with (
            mock.patch("buildscope._command.MAX_COMMAND_CHARS", 8),
            self.assertRaisesRegex(SnapshotError, "character limit"),
        ):
            load_compilation_database(command_database, project_root=project)

    def test_response_files_are_opaque_and_not_executed_or_read(self) -> None:
        project = self._project_layout()
        source = project / "src" / "response.cpp"
        source.write_text("int response;\n", encoding="utf-8")
        sentinel = self.root / "response-must-not-run"
        response = self.root / "flags.rsp"
        response.write_text(f"$(touch {sentinel})\n", encoding="utf-8")
        arguments = ["clang++", f"@{response}", "-c", str(source)]
        database = self._write_database(
            [
                {
                    "directory": str(project / "build"),
                    "file": str(source),
                    "arguments": arguments,
                }
            ]
        )

        entry = load_compilation_database(database, project_root=project)["entries"][0]
        self.assertEqual(entry["normalized"]["argv"], arguments)
        self.assertIn(
            "response-file-opaque",
            {item["code"] for item in entry["diagnostics"]},
        )
        self.assertFalse(sentinel.exists())

    def test_cli_uses_project_root_and_preserves_existing_output_on_failure(self) -> None:
        project = self._project_layout()
        source = project / "src" / "cli.cpp"
        source.write_text("int cli;\n", encoding="utf-8")
        database = self._write_database(
            [
                {
                    "directory": str(project / "build"),
                    "file": str(source),
                    "arguments": ["clang++", "-c", str(source)],
                }
            ],
            path=project / "build" / "compile_commands.json",
        )
        output = self.root / "cli-output.json"
        self.assertEqual(
            main(
                [
                    str(database),
                    "--project-root",
                    str(project),
                    "--output",
                    str(output),
                ]
            ),
            0,
        )
        rendered = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(rendered["source"]["project_root"], str(project.resolve()))
        self.assertEqual(rendered["entries"][0]["normalized"]["source"]["scope"], "project")
        output.write_text("old output\n", encoding="utf-8")
        bad_database = self._write_raw_database(b"{", path=self.root / "bad.json")
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            self.assertEqual(
                main(
                    [
                        str(bad_database),
                        "--project-root",
                        str(project),
                        "--output",
                        str(output),
                    ]
                ),
                2,
            )
        self.assertEqual(output.read_text(encoding="utf-8"), "old output\n")
        self.assertEqual(list(output.parent.glob(f".{output.name}.*.tmp")), [])
        self.assertIn("buildscope:", stderr.getvalue())

    def test_bounded_input_rejects_final_symlink_non_regular_and_oversize(self) -> None:
        target = self.root / "database.json"
        target.write_bytes(b"[]")
        symlink = self.root / "database-link.json"
        try:
            symlink.symlink_to(target)
        except (OSError, NotImplementedError) as error:
            self.skipTest(f"symbolic links are unavailable: {error}")
        with self.assertRaisesRegex(SnapshotIoError, "cannot open compilation database safely"):
            read_bounded_regular(symlink, 16)

        directory = self.root / "database-directory"
        directory.mkdir()
        with self.assertRaisesRegex(SnapshotIoError, "regular file"):
            read_bounded_regular(directory, 16)

        oversized = self.root / "oversized.json"
        oversized.write_bytes(b"12345")
        with self.assertRaisesRegex(SnapshotIoError, "exceeds 4 byte limit"):
            read_bounded_regular(oversized, 4)

    def test_bounded_input_rejects_change_during_read(self) -> None:
        database = self.root / "database.json"
        database.write_bytes(b"[]")
        real_fstat = os.fstat
        for timestamp_index in (8, 9):
            with self.subTest(timestamp_index=timestamp_index):
                calls = 0

                def changing_fstat(
                    descriptor: int, timestamp_index: int = timestamp_index
                ) -> os.stat_result:
                    nonlocal calls
                    calls += 1
                    result = real_fstat(descriptor)
                    if calls != 2:
                        return result
                    values = list(result)
                    values[timestamp_index] += 1
                    return os.stat_result(values)

                with (
                    mock.patch("buildscope._io.os.fstat", side_effect=changing_fstat),
                    self.assertRaisesRegex(
                        SnapshotIoError,
                        "compilation database changed while it was being read",
                    ),
                ):
                    read_bounded_regular(database, 16)
                self.assertEqual(calls, 2)

    def test_bounded_input_rejects_growth_past_limit(self) -> None:
        database = self.root / "database.json"
        database.write_bytes(b"1234")

        with (
            mock.patch("buildscope._io.os.read", return_value=b"12345"),
            self.assertRaisesRegex(SnapshotIoError, "exceeds 4 byte limit"),
        ):
            read_bounded_regular(database, 4)

    def test_cli_refuses_to_overwrite_input_database_aliases(self) -> None:
        database = self._write_database([])
        original = database.read_bytes()
        aliases = [("itself", database)]
        hardlink = self.root / "hardlink.json"
        os.link(database, hardlink)
        aliases.append(("hardlink", hardlink))
        symlink = self.root / "symlink.json"
        try:
            symlink.symlink_to(database)
        except (OSError, NotImplementedError) as error:
            self.skipTest(f"symbolic links are unavailable: {error}")
        aliases.append(("symlink", symlink))

        for alias_name, output in aliases:
            with self.subTest(alias=alias_name):
                stderr = io.StringIO()
                with contextlib.redirect_stderr(stderr):
                    result = main([str(database), "--output", str(output)])
                self.assertEqual(result, 2)
                self.assertEqual(database.read_bytes(), original)
                self.assertIn("must not overwrite the compilation database", stderr.getvalue())
                self.assertEqual(list(self.root.glob(f".{output.name}.*.tmp")), [])

    def test_atomic_replace_failure_preserves_output_and_cleans_temporary_file(self) -> None:
        database = self._write_database([])
        output = self.root / "snapshot.json"
        output.write_text("old snapshot\n", encoding="utf-8")

        with (
            mock.patch("buildscope._io._open_parent_no_follow", return_value=None),
            mock.patch.object(Path, "replace", side_effect=OSError("replace denied")),
            self.assertRaisesRegex(SnapshotIoError, "cannot write snapshot safely"),
        ):
            write_atomic_text(output, "new snapshot\n", protected=database)

        self.assertEqual(output.read_text(encoding="utf-8"), "old snapshot\n")
        self.assertEqual(list(output.parent.glob(f".{output.name}.*.tmp")), [])

    def test_portable_output_pins_the_resolved_parent(self) -> None:
        database = self._write_database([])
        real_parent = self.root / "portable-output"
        real_parent.mkdir()
        linked_parent = self.root / "portable-link"
        try:
            linked_parent.symlink_to(real_parent, target_is_directory=True)
        except (OSError, NotImplementedError) as error:
            self.skipTest(f"symbolic links are unavailable: {error}")
        output = linked_parent / "snapshot.json"

        with mock.patch("buildscope._io._open_parent_no_follow", return_value=None):
            write_atomic_text(output, "{}\n", protected=database)

        self.assertEqual((real_parent / "snapshot.json").read_text(encoding="utf-8"), "{}\n")
        self.assertEqual(list(real_parent.glob(".snapshot.json.*.tmp")), [])

    def test_portable_output_rejects_a_replaced_temporary_file(self) -> None:
        database = self._write_database([])
        output = self.root / "snapshot.json"
        output.write_text("old snapshot\n", encoding="utf-8")
        real_lstat = os.lstat

        def changed_temporary_identity(path: os.PathLike[str] | str) -> os.stat_result:
            result = real_lstat(path)
            if Path(path).name.startswith(f".{output.name}."):
                values = list(result)
                values[1] += 1
                return os.stat_result(values)
            return result

        with (
            mock.patch("buildscope._io._open_parent_no_follow", return_value=None),
            mock.patch("buildscope._io.os.lstat", side_effect=changed_temporary_identity),
            self.assertRaisesRegex(SnapshotIoError, "temporary file changed"),
        ):
            write_atomic_text(output, "new snapshot\n", protected=database)

        self.assertEqual(output.read_text(encoding="utf-8"), "old snapshot\n")
        self.assertEqual(list(output.parent.glob(f".{output.name}.*.tmp")), [])

    def test_atomic_success_replaces_output_with_valid_snapshot(self) -> None:
        project = self._project_layout()
        source = project / "src" / "atomic.cpp"
        source.write_text("int atomic;\n", encoding="utf-8")
        database = self._write_database(
            [
                {
                    "directory": str(project / "build"),
                    "file": str(source),
                    "arguments": ["clang++", "-c", str(source)],
                }
            ],
            path=project / "build" / "compile_commands.json",
        )
        output = self.root / "atomic-snapshot.json"
        output.write_text("old snapshot\n", encoding="utf-8")

        self.assertEqual(
            main(
                [
                    str(database),
                    "--project-root",
                    str(project),
                    "--output",
                    str(output),
                ]
            ),
            0,
        )

        snapshot = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(snapshot["schema_version"], SCHEMA_VERSION)
        self.assertEqual(snapshot["source"]["entry_count"], 1)
        self.assertEqual(snapshot["entries"][0]["normalized"]["source"]["path"], "src/atomic.cpp")
        self.assertEqual(list(output.parent.glob(f".{output.name}.*.tmp")), [])

    @unittest.skipIf(os.name == "nt", "directory-relative no-follow writes are POSIX-only")
    def test_atomic_output_rejects_symlinked_parent(self) -> None:
        database = self._write_database([])
        real_parent = self.root / "real-output"
        real_parent.mkdir()
        linked_parent = self.root / "linked-output"
        try:
            linked_parent.symlink_to(real_parent, target_is_directory=True)
        except (OSError, NotImplementedError) as error:
            self.skipTest(f"symbolic links are unavailable: {error}")

        with self.assertRaisesRegex(SnapshotIoError, "cannot write snapshot safely"):
            write_atomic_text(
                linked_parent / "snapshot.json",
                "{}\n",
                protected=database,
            )
        self.assertEqual(list(real_parent.iterdir()), [])

    def test_snapshot_serializer_enforces_utf8_byte_bound(self) -> None:
        snapshot = {"text": "한글"}
        rendered = dumps_snapshot(snapshot)
        byte_count = len(rendered.encode("utf-8"))

        with (
            mock.patch("buildscope.snapshot.MAX_SNAPSHOT_BYTES", byte_count - 1),
            self.assertRaisesRegex(SnapshotError, "exceeds .* byte limit"),
        ):
            dumps_snapshot(snapshot)
        with mock.patch("buildscope.snapshot.MAX_SNAPSHOT_BYTES", byte_count):
            self.assertEqual(dumps_snapshot(snapshot), rendered)

    def test_cli_snapshot_bound_preserves_existing_output(self) -> None:
        database = self._write_database([])
        output = self.root / "snapshot.json"
        output.write_text("old snapshot\n", encoding="utf-8")
        stderr = io.StringIO()

        with (
            mock.patch("buildscope.snapshot.MAX_SNAPSHOT_BYTES", 1),
            contextlib.redirect_stderr(stderr),
        ):
            self.assertEqual(main([str(database), "--output", str(output)]), 2)

        self.assertEqual(output.read_text(encoding="utf-8"), "old snapshot\n")
        self.assertEqual(list(output.parent.glob(f".{output.name}.*.tmp")), [])
        self.assertIn("snapshot exceeds 1 byte limit", stderr.getvalue())

    def test_foreign_windows_path_scopes_are_lexical_and_case_insensitive(self) -> None:
        vendor = path_record(
            r"..\Vendor\sdk",
            base="C:/Repo/build",
            project_root="c:/repo",
            style="windows",
            expected="directory",
        )
        root = path_record(
            "C:/Repo",
            base="C:/Repo/build",
            project_root="c:/repo",
            style="windows",
            expected=None,
        )
        outside = path_record(
            "D:/SDK/include",
            base="C:/Repo/build",
            project_root="c:/repo",
            style="windows",
            expected="directory",
        )

        self.assertEqual(
            vendor, {"exists": None, "path": "Vendor/sdk", "scope": "vendor", "style": "windows"}
        )
        self.assertEqual(root["path"], ".")
        self.assertEqual(root["scope"], "project")
        self.assertEqual(outside["path"], "D:/SDK/include")
        self.assertEqual(outside["scope"], "system")

    def test_normalized_paths_are_field_bounded(self) -> None:
        database = self._write_database(
            [{"directory": "/work", "file": "long.cpp", "arguments": ["cc"]}]
        )

        with (
            mock.patch("buildscope._paths.MAX_PATH_CHARS", 2),
            self.assertRaisesRegex(SnapshotError, "normalized path exceeds"),
        ):
            load_compilation_database(database)

    def test_low_level_command_bounds_and_compiler_families(self) -> None:
        self.assertEqual(split_windows_command('cl.exe "a""b" ""'), ["cl.exe", 'a"b', ""])
        self.assertEqual(compiler_record(["x86_64-linux-gnu-g++"])["family"], "gcc")
        self.assertEqual(compiler_record(["vendor-clang"])["family"], "clang")
        self.assertEqual(compiler_record(["custom-compiler"])["family"], "unknown")
        self.assertEqual(
            compiler_record(["env", "--ignore-environment", "--unset=TOKEN", "sccache"])[
                "wrappers"
            ],
            ["env", "sccache"],
        )
        with self.assertRaisesRegex(CommandError, "name a compiler"):
            parse_invocation({"directory": "/work", "file": "a.cpp", "arguments": [""]}, 3)
        with (
            mock.patch("buildscope._command.MAX_ARGUMENT_CHARS", 2),
            self.assertRaisesRegex(CommandError, "character limit"),
        ):
            parse_invocation(
                {"directory": "/work", "file": "a.cpp", "arguments": ["clang++"]},
                4,
            )
        with (
            mock.patch("buildscope._command.MAX_ARGUMENTS", 1),
            self.assertRaisesRegex(CommandError, "invalid bounded argv"),
        ):
            parse_invocation(
                {"directory": "/work", "file": "a.cpp", "command": "clang++ a.cpp"},
                5,
            )

    def test_output_parser_stops_at_terminator_and_avoids_long_option_prefixes(self) -> None:
        self.assertEqual(output_from_argv(["cc", "-object-file-dir=/tmp", "a.cpp"]), "")
        self.assertEqual(output_from_argv(["cc", "--", "-o", "operand"]), "")
        self.assertEqual(output_from_argv(["cc", "-oresult.o", "a.cpp"]), "")
        self.assertEqual(output_from_argv(["cl", "/Foresult.obj", "a.cpp"]), "result.obj")
        self.assertEqual(output_from_argv(["cl", "/Fo:result.obj", "a.cpp"]), "result.obj")
        self.assertEqual(output_from_argv(["cl", "/Fo:", "result.obj", "a.cpp"]), "result.obj")
        self.assertEqual(output_from_argv(["cl", "/format", "a.cpp"]), "")

    def test_msvc_options_are_case_sensitive_and_do_not_consume_other_switches(self) -> None:
        metadata = extract_metadata(
            ["cl", "/utf-8", "/interface", "/diagnostics:caret", "source.cpp"],
            "source.cpp",
        )

        self.assertEqual(metadata["defines"], [])
        self.assertEqual(metadata["include_paths"], [])
        self.assertEqual(metadata["diagnostics"], [])

    def test_windows_gcc_path_selects_windows_command_style(self) -> None:
        argv, style, _, _ = parse_invocation(
            {
                "arguments": [r"C:\MinGW\bin\g++.exe", "-c", r"src\main.cpp"],
                "directory": "build",
                "file": r"src\main.cpp",
            },
            0,
        )

        self.assertEqual(style, "windows")
        self.assertEqual(argv[0], r"C:\MinGW\bin\g++.exe")

    def test_metadata_diagnostics_variants_and_path_failures(self) -> None:
        diagnostic_argv = (
            ["clang++", "-std"],
            ["clang++", "-xfortran"],
            ["clang++", "-D1INVALID"],
            ["clang++", "-I"],
            ["clang++", "--sysroot"],
            ["clang++", "--target"],
            ["clang++", "@one.rsp", "@two.rsp"],
        )
        diagnostics = [
            item
            for argv in diagnostic_argv
            for item in extract_metadata(argv, "source.unknown")["diagnostics"]
        ]
        codes = [item["code"] for item in diagnostics]
        self.assertIn("missing-standard", codes)
        self.assertIn("unknown-language", codes)
        self.assertIn("invalid-define", codes)
        self.assertIn("missing-include", codes)
        self.assertIn("missing-sysroot", codes)
        self.assertIn("missing-target", codes)
        self.assertEqual(codes.count("response-file-opaque"), 1)
        self.assertEqual(
            extract_metadata(["cl.exe", "/Tcsource.c"], "source.unknown")["language"], "c"
        )
        self.assertEqual(
            extract_metadata(["cl.exe", "/Tpsource.cpp"], "source.unknown")["language"], "c++"
        )

        variants = extract_metadata(
            [
                "clang++",
                "-idirafter/after",
                "-iframework/framework",
                "-Fframework-two",
                "-isysroot/sdk",
                "-target=arm64-apple-darwin",
            ],
            "source.cpp",
        )
        self.assertEqual(
            [item["kind"] for item in variants["include_paths"]],
            ["after", "framework", "framework"],
        )
        self.assertEqual(variants["sysroot"], "/sdk")
        self.assertEqual(variants["target_triple"], "arm64-apple-darwin")
        self.assertEqual(output_from_argv(["cl.exe", "/Foone.obj", "/Fo", "two.obj"]), "two.obj")

        with mock.patch("pathlib.Path.resolve", side_effect=OSError("unreadable")):
            self.assertEqual(_native_record("/tmp/a", "/tmp", "file"), ("/tmp/a", None, None))
        with mock.patch("pathlib.Path.is_file", side_effect=OSError("unreadable")):
            self.assertIsNone(native_mtime("missing.cpp", str(self.root), "posix"))


if __name__ == "__main__":
    unittest.main()
