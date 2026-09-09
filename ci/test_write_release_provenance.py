#!/usr/bin/env python3
"""Contract tests for the release provenance record."""

from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(__file__).resolve().parent))

from write_release_provenance import (  # noqa: E402
    ProvenanceError,
    build_provenance,
)

ENV = {
    "TAG": "abilens-v0.1.0",
    "VERSION": "0.1.0",
    "TARGET_SHA": "a" * 40,
    "MAIN_SHA": "a" * 40,
    "MERGE_GATE_URL": "https://example.invalid/check",
    "GITHUB_RUN_ID": "12345",
    "GITHUB_SERVER_URL": "https://github.com",
    "GITHUB_REPOSITORY": "owner/repo",
    "ICI_VERSION": "v0.10.2",
    "ICI_PYZ_SHA256": "b" * 64,
}


class ProvenanceTests(unittest.TestCase):
    def test_records_what_was_released_and_from_where(self) -> None:
        payload = build_provenance("abilens", ENV)
        self.assertEqual(payload["product"], "abilens")
        self.assertEqual(payload["version"], "0.1.0")
        self.assertEqual(payload["tag"], "abilens-v0.1.0")
        self.assertEqual(payload["target_commit"], "a" * 40)
        self.assertEqual(payload["ici"]["sha256"], "b" * 64)
        self.assertEqual(payload["workflow"]["run_id"], "12345")

    def test_rejects_a_tag_that_does_not_belong_to_this_product(self) -> None:
        """A buildscope tag must not produce an abilens provenance record.

        The wrong-prefix case is diagnosed separately from the wrong-version
        case on purpose: "does not belong to abilens" names the mistake, where
        a version mismatch would send the reader looking at the version. The
        assertion pins that distinction, otherwise the prefix check could be
        deleted and the version check would absorb it unnoticed.
        """

        env = dict(ENV, TAG="buildscope-v0.1.0")
        with self.assertRaisesRegex(ProvenanceError, r"does not belong to abilens"):
            build_provenance("abilens", env)

    def test_rejects_a_tag_whose_version_is_not_the_released_version(self) -> None:
        env = dict(ENV, TAG="abilens-v9.9.9")
        with self.assertRaisesRegex(ProvenanceError, "version"):
            build_provenance("abilens", env)

    def test_rejects_a_release_not_built_from_exact_main(self) -> None:
        """Provenance that records a non-main commit is a false claim."""

        env = dict(ENV, MAIN_SHA="c" * 40)
        with self.assertRaisesRegex(ProvenanceError, "main"):
            build_provenance("abilens", env)

    def test_rejects_a_missing_required_field(self) -> None:
        for key in ("TARGET_SHA", "GITHUB_RUN_ID", "ICI_PYZ_SHA256"):
            with self.subTest(missing=key):
                env = {k: v for k, v in ENV.items() if k != key}
                with self.assertRaises(ProvenanceError):
                    build_provenance("abilens", env)

    def test_rejects_a_malformed_commit_or_digest(self) -> None:
        for key, value in (
            ("TARGET_SHA", "not-a-sha"),
            ("MAIN_SHA", "not-a-sha"),
            ("ICI_PYZ_SHA256", "xyz"),
        ):
            with self.subTest(field=key):
                env = dict(ENV, **{key: value})
                if key == "MAIN_SHA":
                    env["TARGET_SHA"] = value
                with self.assertRaises(ProvenanceError):
                    build_provenance("abilens", env)

    def test_serialized_record_is_deterministic(self) -> None:
        """Two runs of the same release must not differ in the provenance bytes."""

        from write_release_provenance import serialize

        first = serialize(build_provenance("abilens", ENV))
        second = serialize(build_provenance("abilens", ENV))
        self.assertEqual(first, second)
        self.assertTrue(first.endswith("\n"))
        json.loads(first)

    def test_writes_the_record_where_the_workflow_expects_it(self) -> None:
        from write_release_provenance import main

        with TemporaryDirectory() as tmp:
            destination = Path(tmp) / "abilens-provenance.json"
            exit_code = main(["abilens", str(destination)], env=ENV)
            self.assertEqual(exit_code, 0)
            payload = json.loads(destination.read_text(encoding="utf-8"))
            self.assertEqual(payload["product"], "abilens")


if __name__ == "__main__":
    unittest.main()
