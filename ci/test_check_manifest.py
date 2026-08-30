"""Stdlib tests for the manifest-to-matrix discovery contract."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path


CI_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(CI_DIR))

from check_manifest import SUPPORTED_QT_MAJORS, discover  # noqa: E402


class ManifestDiscoveryTests(unittest.TestCase):
    def test_current_gui_projects_expand_to_both_qt_majors(self) -> None:
        verify_projects, gui_projects, names = discover(CI_DIR.parent)

        self.assertEqual(names, ["diskmap", "loglens"])
        self.assertEqual([item["name"] for item in verify_projects], names)
        self.assertEqual(
            [(item["name"], item["qt_major"]) for item in gui_projects],
            [
                ("diskmap", 5),
                ("diskmap", 6),
                ("loglens", 5),
                ("loglens", 6),
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
                                    "smoke_arg": "fixtures",
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


if __name__ == "__main__":
    unittest.main()
