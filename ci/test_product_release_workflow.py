#!/usr/bin/env python3
"""Static contract tests for the shared product release workflow.

A release workflow cannot be run to check it — running it publishes something.
These pin the properties that make publication safe, so a well-meaning edit that
removes one fails here instead of on a public release.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
WORKFLOW_PATH = REPO_ROOT / ".github" / "workflows" / "product-release.yml"
WORKFLOW = WORKFLOW_PATH.read_text(encoding="utf-8")


def _job_block(name: str) -> str:
    marker = f"  {name}:\n"
    start = WORKFLOW.index(marker)
    remainder = WORKFLOW[start + len(marker) :]
    next_job = re.search(r"(?m)^  [a-z0-9][a-z0-9-]*:\n", remainder)
    end = start + len(marker) + (next_job.start() if next_job else len(remainder))
    return WORKFLOW[start:end]


class ProductReleaseWorkflowTests(unittest.TestCase):
    def test_only_the_publish_job_may_write(self) -> None:
        """Building must not be able to touch the release slot."""

        self.assertRegex(WORKFLOW, r"(?m)^permissions:\n  contents: read\n")
        for job in ("provenance", "build"):
            with self.subTest(job=job):
                block = _job_block(job)
                self.assertIn("contents: read", block)
                self.assertNotIn("contents: write", block)
        self.assertIn("contents: write", _job_block("publish"))

    def test_every_job_checks_out_the_validated_commit_without_credentials(self) -> None:
        for job in ("build", "publish"):
            with self.subTest(job=job):
                block = _job_block(job)
                self.assertIn("ref: ${{ needs.provenance.outputs.target_sha }}", block)
                self.assertIn("persist-credentials: false", block)
                self.assertIn('test "$(git rev-parse HEAD)" = "$TARGET_SHA"', block)

    def test_provenance_requires_an_annotated_tag_on_exact_main(self) -> None:
        block = _job_block("provenance")
        self.assertIn("Releases require an annotated tag", block)
        self.assertIn('if [ "$target_sha" != "$main_sha" ]', block)
        self.assertIn("check_buildscope_merge_gate.py", block)

    def test_provenance_refuses_before_anything_is_built(self) -> None:
        """Metadata and notes are gated in provenance, not discovered at publish."""

        block = _job_block("provenance")
        self.assertIn("ci/check_release_metadata.py", block)
        self.assertIn("ci/extract_release_notes.py", block)

    def test_a_tag_alone_does_not_make_a_product_releasable(self) -> None:
        block = _job_block("provenance")
        self.assertIn("ci/projects.json", block)
        self.assertIn("is not a released product", block)

    def test_the_pinned_ici_is_checked_twice_against_one_literal(self) -> None:
        block = _job_block("build")
        self.assertIn('test "$published" = "$ICI_PYZ_SHA256"', block)
        self.assertIn('test "$actual" = "$ICI_PYZ_SHA256"', block)

    def test_the_products_own_gate_runs_before_ici_looks_at_it(self) -> None:
        block = _job_block("build")
        gate = block.index("make -j")
        deep = block.index("--profile deep")
        self.assertLess(gate, deep, "ici deep must not run before the native gate")

    def test_the_bundle_is_deterministic(self) -> None:
        block = _job_block("build")
        self.assertIn("--sort=name", block)
        self.assertIn("--mtime='@0'", block)
        self.assertIn("--numeric-owner", block)
        self.assertIn("gzip -n", block)

    def test_the_test_fixture_is_kept_out_of_the_bundle(self) -> None:
        """abilens builds a shared object for tests; shipping it would be wrong."""

        block = _job_block("build")
        self.assertIn('test ! -e "$staging/lib/lib${PRODUCT}-fixture.so"', block)

    def test_publication_goes_through_a_private_draft(self) -> None:
        block = _job_block("publish")
        self.assertIn('"draft": True', block)
        publish = block.index("Publish the audited draft")
        audit = block.index("Audit the private draft before publishing")
        self.assertLess(audit, publish, "the draft must be audited before it is published")

    def test_the_draft_carries_this_runs_ownership_marker(self) -> None:
        block = _job_block("publish")
        self.assertIn("-release-owner:%s:%s:%s", block)
        self.assertIn("--expected-owner-marker", block)
        self.assertIn("--expected-body-sha256", block)

    def test_assets_are_re_audited_after_crossing_a_job_boundary(self) -> None:
        block = _job_block("publish")
        self.assertIn("Re-audit the downloaded assets against their manifest", block)
        self.assertIn("sha256sum --check SHA256SUMS", block)

    def test_the_public_release_is_downloaded_back_and_audited(self) -> None:
        block = _job_block("publish")
        final = block.index("Independently download and audit the public release")
        self.assertIn("gh release download", block[final:])
        self.assertIn("--stage final", block[final:])
        for checker in ("release_state.py", "release_assets.py", "release_manifest.py"):
            with self.subTest(checker=checker):
                self.assertIn(checker, block[final:])

    def test_a_failed_draft_is_preserved_rather_than_deleted(self) -> None:
        block = _job_block("publish")
        self.assertIn("failure()", block)
        self.assertIn("was preserved for review", block)
        self.assertNotIn("gh release delete", block)

    def test_tag_patterns_only_cover_products_the_manifest_releases(self) -> None:
        import json

        manifest = json.loads((REPO_ROOT / "ci" / "projects.json").read_text(encoding="utf-8"))
        released = {
            project["name"]
            for project in manifest["projects"]
            if project.get("release", {}).get("enabled")
        }
        patterns = re.findall(r'^      - "([a-z0-9]+)-v\*\.\*\.\*"$', WORKFLOW, re.MULTILINE)
        self.assertTrue(patterns, "the workflow must declare at least one tag pattern")
        for product in patterns:
            with self.subTest(product=product):
                self.assertIn(product, released)
        self.assertNotIn(
            "buildscope",
            patterns,
            "buildscope keeps its own workflow; two workflows on one tag would race",
        )


if __name__ == "__main__":
    unittest.main()
