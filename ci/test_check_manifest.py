"""Stdlib tests for the manifest-to-matrix discovery contract."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

CI_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(CI_DIR))

from check_manifest import SUPPORTED_QT_MAJORS, discover


class ManifestDiscoveryTests(unittest.TestCase):
    @staticmethod
    def _manifest_for(*names: str) -> dict[str, object]:
        return {
            "schema": 1,
            "projects": [
                {
                    "name": name,
                    "verify": True,
                    "gui": {
                        "enabled": True,
                        "build_system": "cmake",
                        "build_descriptor": "CMakeLists.txt",
                        "binary": "build/gui/sample",
                        "smoke_arg": "fixtures/sample.json",
                    },
                }
                for name in names
            ],
        }

    @staticmethod
    def _write_repository(
        root: Path,
        manifest: object,
        *,
        project_names: tuple[str, ...] = ("sample",),
        descriptors: tuple[str, ...] = ("CMakeLists.txt",),
        smoke_files: tuple[str, ...] = ("fixtures/sample.json",),
    ) -> None:
        (root / "ci").mkdir()
        for name in project_names:
            project = root / name
            project.mkdir()
            (project / "ici.toml").write_text(f'name = "{name}"\n', encoding="utf-8")
            for descriptor in descriptors:
                descriptor_path = project / descriptor
                descriptor_path.parent.mkdir(parents=True, exist_ok=True)
                descriptor_path.write_text(
                    "cmake_minimum_required(VERSION 3.16)\n", encoding="utf-8"
                )
            for smoke_file in smoke_files:
                smoke_path = project / smoke_file
                smoke_path.parent.mkdir(parents=True, exist_ok=True)
                smoke_path.write_text("sample\n", encoding="utf-8")
        (root / "ci" / "projects.json").write_text(
            json.dumps(manifest), encoding="utf-8"
        )

    def test_current_projects_expand_gui_projects_to_both_qt_majors(self) -> None:
        verify_projects, gui_projects, names = discover(CI_DIR.parent)

        self.assertEqual(names, ["diskmap", "loglens", "buildscope", "envlens"])
        self.assertEqual([item["name"] for item in verify_projects], names)
        self.assertNotIn("envlens", [item["name"] for item in gui_projects])
        self.assertEqual(
            [
                (
                    item["name"],
                    item["build_system"],
                    item["build_descriptor"],
                    item["gui_binary"],
                    item["smoke_arg"],
                    item["qt_major"],
                )
                for item in gui_projects
            ],
            [
                (
                    "diskmap",
                    "qmake",
                    "diskmap.pro",
                    "build/gui/src/gui/diskmap-gui",
                    "src",
                    5,
                ),
                (
                    "diskmap",
                    "qmake",
                    "diskmap.pro",
                    "build/gui/src/gui/diskmap-gui",
                    "src",
                    6,
                ),
                (
                    "loglens",
                    "cmake",
                    "CMakeLists.txt",
                    "build/gui/src/gui/loglens-gui",
                    "tests/data/sample.log",
                    5,
                ),
                (
                    "loglens",
                    "cmake",
                    "CMakeLists.txt",
                    "build/gui/src/gui/loglens-gui",
                    "tests/data/sample.log",
                    6,
                ),
                (
                    "buildscope",
                    "cmake",
                    "CMakeLists.txt",
                    "build/gui/src/gui/buildscope-gui",
                    "fixtures/sample.snapshot.json",
                    5,
                ),
                (
                    "buildscope",
                    "cmake",
                    "CMakeLists.txt",
                    "build/gui/src/gui/buildscope-gui",
                    "fixtures/sample.snapshot.json",
                    6,
                ),
            ],
        )

    def test_new_gui_project_inherits_the_supported_major_set(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "ci").mkdir()
            project = root / "sample"
            project.mkdir()
            (project / "ici.toml").write_text(
                '[project]\nname = "sample"\n', encoding="utf-8"
            )
            (project / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.16)\n", encoding="utf-8"
            )
            (project / "fixtures").mkdir()
            (project / "fixtures" / "sample.json").write_text("{}\n", encoding="utf-8")
            (root / "ci" / "projects.json").write_text(
                json.dumps(
                    {
                        "schema": 1,
                        "projects": [
                            {
                                "name": "sample",
                                "verify": True,
                                "gui": {
                                    "enabled": True,
                                    "build_system": "cmake",
                                    "build_descriptor": "CMakeLists.txt",
                                    "binary": "build/gui/sample",
                                    "smoke_arg": "fixtures/sample.json",
                                },
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )

            _, gui_projects, names = discover(root)

        self.assertEqual(names, ["sample"])
        self.assertEqual(
            [item["qt_major"] for item in gui_projects], list(SUPPORTED_QT_MAJORS)
        )

    def test_non_gui_project_stays_in_verify_matrix_without_gui_entries(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = self._manifest_for("sample")
            manifest["projects"][0]["gui"]["enabled"] = False
            self._write_repository(
                root,
                manifest,
                descriptors=(),
                smoke_files=(),
            )

            verify_projects, gui_projects, names = discover(root)

        self.assertEqual(names, ["sample"])
        self.assertEqual(verify_projects, [{"name": "sample"}])
        self.assertEqual(gui_projects, [])

    def test_manifest_root_must_be_an_object(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "ci").mkdir()
            (root / "ci" / "projects.json").write_text("[]", encoding="utf-8")

            with self.assertRaisesRegex(TypeError, "root must be an object"):
                discover(root)

    def test_manifest_rejects_missing_or_extra_projects(self) -> None:
        cases = [
            ("extra project directory", ("sample",), ("sample", "extra")),
            ("missing project directory", ("sample", "extra"), ("sample",)),
        ]
        for label, manifest_names, project_names in cases:
            with self.subTest(label=label), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                self._write_repository(
                    root,
                    self._manifest_for(*manifest_names),
                    project_names=project_names,
                )

                with self.assertRaisesRegex(
                    ValueError, "manifest/project mismatch|must be a directory"
                ):
                    discover(root)

    def test_manifest_rejects_duplicate_project_names(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self._write_repository(root, self._manifest_for("sample", "sample"))

            with self.assertRaisesRegex(ValueError, "duplicate project name"):
                discover(root)

    def test_manifest_rejects_invalid_gui_metadata(self) -> None:
        cases = [
            ("missing gui", lambda entry: entry.pop("gui"), TypeError),
            (
                "non-boolean enabled",
                lambda entry: entry["gui"].update(enabled="yes"),
                TypeError,
            ),
            (
                "unsupported build system",
                lambda entry: entry["gui"].update(build_system="ninja"),
                ValueError,
            ),
            (
                "verification disabled",
                lambda entry: entry.update(verify=False),
                ValueError,
            ),
        ]
        for label, mutate, expected_error in cases:
            with self.subTest(label=label), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                manifest = self._manifest_for("sample")
                mutate(manifest["projects"][0])
                self._write_repository(root, manifest)

                with self.assertRaises(expected_error):
                    discover(root)

    def test_manifest_rejects_build_system_descriptor_mismatch(self) -> None:
        cases = [
            ("cmake", "sample.pro", "CMake GUI descriptor"),
            ("qmake", "CMakeLists.txt", "qmake GUI descriptor"),
        ]
        for build_system, descriptor, message in cases:
            with (
                self.subTest(build_system=build_system),
                tempfile.TemporaryDirectory() as temporary,
            ):
                root = Path(temporary)
                manifest = self._manifest_for("sample")
                manifest["projects"][0]["gui"].update(
                    build_system=build_system,
                    build_descriptor=descriptor,
                )
                self._write_repository(
                    root,
                    manifest,
                    descriptors=("CMakeLists.txt", "sample.pro"),
                )

                with self.assertRaisesRegex(ValueError, message):
                    discover(root)

    def test_manifest_rejects_unsafe_paths(self) -> None:
        cases = [
            (
                "qmake descriptor absolute",
                "qmake",
                "build_descriptor",
                "/tmp/sample.pro",
            ),
            (
                "qmake descriptor traversal",
                "qmake",
                "build_descriptor",
                "../sample.pro",
            ),
            ("binary absolute", "cmake", "binary", "/tmp/buildscope"),
            ("binary traversal", "cmake", "binary", "../buildscope"),
            ("smoke absolute", "cmake", "smoke_arg", "/tmp/input"),
            ("smoke traversal", "cmake", "smoke_arg", "../input"),
        ]
        for label, build_system, field, value in cases:
            with self.subTest(label=label), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                manifest = self._manifest_for("sample")
                gui = manifest["projects"][0]["gui"]
                gui["build_system"] = build_system
                if build_system == "qmake":
                    gui["build_descriptor"] = "sample.pro"
                gui[field] = value
                self._write_repository(
                    root,
                    manifest,
                    descriptors=("CMakeLists.txt", "sample.pro"),
                )

                with self.assertRaisesRegex(ValueError, "invalid|unsafe"):
                    discover(root)

    def test_manifest_rejects_missing_or_non_regular_descriptor(self) -> None:
        cases = [("missing", False), ("directory", True)]
        for label, make_directory in cases:
            with self.subTest(label=label), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                self._write_repository(
                    root, self._manifest_for("sample"), descriptors=()
                )
                descriptor = root / "sample" / "CMakeLists.txt"
                if make_directory:
                    descriptor.mkdir()

                with self.assertRaisesRegex(ValueError, "does not exist|regular file"):
                    discover(root)

    def test_manifest_rejects_missing_smoke_input(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self._write_repository(
                root,
                self._manifest_for("sample"),
                smoke_files=(),
            )

            with self.assertRaisesRegex(ValueError, "does not exist"):
                discover(root)

    def test_manifest_rejects_paths_resolving_outside_project(self) -> None:
        cases = [
            ("descriptor", "CMakeLists.txt", "outside-descriptor.txt"),
            ("smoke input", "fixtures/sample.json", "outside-smoke.json"),
        ]
        for label, relative_path, outside_name in cases:
            with self.subTest(label=label), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                self._write_repository(root, self._manifest_for("sample"))
                outside = root / outside_name
                outside.write_text("outside\n", encoding="utf-8")
                linked_path = root / "sample" / relative_path
                linked_path.unlink()
                linked_path.symlink_to(outside)

                with self.assertRaisesRegex(ValueError, "unsafe"):
                    discover(root)


if __name__ == "__main__":
    unittest.main()
