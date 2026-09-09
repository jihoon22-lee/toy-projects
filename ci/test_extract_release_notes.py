#!/usr/bin/env python3
"""Contract tests for CHANGELOG release-note extraction."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(__file__).resolve().parent))

from extract_release_notes import (  # noqa: E402
    ReleaseNotesError,
    extract_release_notes,
)

CHANGELOG = """# Changelog

### AbiLens 0.1.0

- shipped the ELF inspector
- documented the supported binutils range

### LogLens 0.2.0

- loglens only

## [0.5.0] - 2026-09-02

- an old repo-level heading
"""


class ReleaseNotesTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "CHANGELOG.md").write_text(CHANGELOG, encoding="utf-8")

    def test_returns_only_this_products_section(self) -> None:
        notes = extract_release_notes(self.root, "AbiLens", "0.1.0")
        self.assertIn("shipped the ELF inspector", notes)
        self.assertNotIn("loglens only", notes)
        self.assertNotIn("old repo-level heading", notes)

    def test_stops_at_the_next_heading_of_any_level(self) -> None:
        notes = extract_release_notes(self.root, "LogLens", "0.2.0")
        self.assertEqual(notes.strip(), "- loglens only")

    def test_rejects_a_version_with_no_section(self) -> None:
        with self.assertRaises(ReleaseNotesError):
            extract_release_notes(self.root, "AbiLens", "9.9.9")

    def test_rejects_another_products_section_for_this_version(self) -> None:
        """LogLens 0.2.0 must not become AbiLens 0.2.0's release notes."""

        with self.assertRaises(ReleaseNotesError):
            extract_release_notes(self.root, "AbiLens", "0.2.0")

    def test_rejects_an_empty_section(self) -> None:
        (self.root / "CHANGELOG.md").write_text(
            "# Changelog\n\n### AbiLens 0.1.0\n\n### LogLens 0.2.0\n\n- x\n",
            encoding="utf-8",
        )
        with self.assertRaisesRegex(ReleaseNotesError, "empty"):
            extract_release_notes(self.root, "AbiLens", "0.1.0")

    def test_rejects_a_duplicated_section(self) -> None:
        (self.root / "CHANGELOG.md").write_text(
            "# Changelog\n\n### AbiLens 0.1.0\n\n- a\n\n### AbiLens 0.1.0\n\n- b\n",
            encoding="utf-8",
        )
        with self.assertRaisesRegex(ReleaseNotesError, "more than once"):
            extract_release_notes(self.root, "AbiLens", "0.1.0")


if __name__ == "__main__":
    unittest.main()
