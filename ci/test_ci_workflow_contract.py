"""Static regression tests for the event-specific report publication boundary."""

from __future__ import annotations

import re
import unittest
from pathlib import Path

WORKFLOW = (
    Path(__file__).resolve().parents[1] / ".github" / "workflows" / "ci.yml"
).read_text(encoding="utf-8")
RELEASE_WORKFLOW = (
    Path(__file__).resolve().parents[1]
    / ".github"
    / "workflows"
    / "buildscope-release.yml"
).read_text(encoding="utf-8")


def _job_block(name: str) -> str:
    marker = f"  {name}:\n"
    start = WORKFLOW.index(marker)
    remainder = WORKFLOW[start + len(marker) :]
    next_job = re.search(r"(?m)^  [a-z0-9][a-z0-9-]*:\n", remainder)
    end = start + len(marker) + (next_job.start() if next_job else len(remainder))
    return WORKFLOW[start:end]


class WorkflowPublicationContractTests(unittest.TestCase):
    def test_pull_request_publisher_is_pr_only_and_serialized(self) -> None:
        block = _job_block("report-pr")

        self.assertIn("github.event_name == 'pull_request'", block)
        self.assertIn("group: gh-pages-${{ github.repository }}", block)

    def test_main_publisher_is_exact_main_only_and_waits_for_quality(self) -> None:
        block = _job_block("publish-main")

        self.assertIn("always() && github.event_name == 'push'", block)
        self.assertIn("github.ref == 'refs/heads/main'", block)
        self.assertIn("group: gh-pages-${{ github.repository }}", block)
        for dependency in (
            "discover",
            "verify",
            "gui-build",
            "benchmark-smoke",
            "diskmap-benchmark-smoke",
            "buildscope-benchmark-smoke",
            "buildscope-ici-deep",
            "buildscope-python-quality",
            "buildscope-release-contract",
        ):
            self.assertIn(f"      - {dependency}\n", block)

        self.assertIn("ref: ${{ github.sha }}", block)
        self.assertIn('test "$(git rev-parse HEAD)" = "$EXPECTED_SHA"', block)
        self.assertGreaterEqual(block.count('test "$remote_main" = "$EXPECTED_SHA"'), 2)

    def test_main_publisher_pins_ici_and_proves_exact_public_bytes(self) -> None:
        block = _job_block("publish-main")

        self.assertIn('test "$manifest_hash" = "$ICI_SHA256"', block)
        self.assertIn("sha256sum --check ici.pyz.sha256", block)
        self.assertIn(
            'args+=(--report-dir "$project=$GITHUB_WORKSPACE/reports/$project")',
            block,
        )
        self.assertIn(
            'cmp -s "reports/$project/verify_report.html" "$body_file"', block
        )
        self.assertIn(
            'python3 ci/check_published_html.py "$body_file" --project "$project"',
            block,
        )

    def test_merge_gate_requires_the_correct_event_publisher(self) -> None:
        block = _job_block("merge-gate")

        self.assertIn("publish-main", block)
        self.assertIn('test "$REPORT_RESULT" = success', block)
        self.assertIn('test "$MAIN_PUBLISH_RESULT" = skipped', block)
        self.assertIn('test "$EVENT_NAME" = push', block)
        self.assertIn('test "$REPORT_RESULT" = skipped', block)
        self.assertIn('test "$MAIN_PUBLISH_RESULT" = success', block)

    def test_discovery_executes_this_contract_suite(self) -> None:
        block = _job_block("discover")

        self.assertIn("ci/test_ci_workflow_contract.py", block)

    def test_buildscope_release_invokes_the_tested_asset_auditor(self) -> None:
        self.assertIn("python3 ci/check_buildscope_release_assets.py", RELEASE_WORKFLOW)
        self.assertIn(
            '"$RUNNER_TEMP/buildscope-release.json" dist "$TAG" "$VERSION"',
            RELEASE_WORKFLOW,
        )
        self.assertNotIn(
            'if python3 - "$RUNNER_TEMP/buildscope-release.json"',
            RELEASE_WORKFLOW,
        )
        self.assertIn("ci/test_check_buildscope_release_assets.py", WORKFLOW)


if __name__ == "__main__":
    unittest.main()
