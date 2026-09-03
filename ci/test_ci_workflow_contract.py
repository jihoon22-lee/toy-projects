"""Static regression tests for the event-specific report publication boundary."""

from __future__ import annotations

import json
import re
import subprocess
import sys
import tempfile
import textwrap
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


def _sticky_verifier_script() -> str:
    block = _job_block("report-pr")
    start_marker = (
        '              "$PROJECT_NAMES" "$EXPECTED_REPORTS" "$RUN_URL" <<\'PY\'\n'
    )
    start = block.index(start_marker) + len(start_marker)
    end = block.index("\n          PY\n", start)
    return textwrap.dedent(block[start:end])


def _run_sticky_verifier(
    comments_pages: list[list[dict[str, str]]],
    *,
    names: str = "diskmap,loglens,buildscope,envlens",
    expected_count: int = 4,
) -> subprocess.CompletedProcess[str]:
    run_url = "https://github.com/example/toy-projects/actions/runs/99"
    with tempfile.TemporaryDirectory() as temporary_directory:
        temporary_path = Path(temporary_directory)
        comments_path = temporary_path / "comments.json"
        comment_path = temporary_path / "comment.md"
        comments_path.write_text(
            json.dumps(comments_pages),
            encoding="utf-8",
        )
        return subprocess.run(
            [
                sys.executable,
                "-c",
                _sticky_verifier_script(),
                str(comments_path),
                str(comment_path),
                "https://pages.example/toy-projects",
                "42",
                names,
                str(expected_count),
                run_url,
            ],
            capture_output=True,
            check=False,
            text=True,
        )


def _release_step_block(name: str) -> str:
    marker = f"      - name: {name}\n"
    start = RELEASE_WORKFLOW.index(marker)
    remainder = RELEASE_WORKFLOW[start + len(marker) :]
    next_step = re.search(r"(?m)^      - name: .+\n", remainder)
    end = start + len(marker) + (next_step.start() if next_step else len(remainder))
    return RELEASE_WORKFLOW[start:end]


class WorkflowPublicationContractTests(unittest.TestCase):
    def test_pull_request_publisher_is_pr_only_and_serialized(self) -> None:
        block = _job_block("report-pr")

        self.assertIn("github.event_name == 'pull_request'", block)
        self.assertIn("group: gh-pages-${{ github.repository }}", block)

    def test_pr_sticky_comment_requires_one_marker_in_one_comment(self) -> None:
        block = _job_block("report-pr")

        self.assertIn('marker = "<!-- ici-report -->"', block)
        self.assertIn("marker_count = sum(", block)
        self.assertIn("if len(marked) != 1 or marker_count != 1:", block)
        self.assertIn("sticky comment cardinality mismatch", block)
        self.assertIn('body = marked[0].get("body") or ""', block)
        self.assertNotIn("most recently updated marker", block)
        self.assertNotIn("marked.sort(", block)
        self.assertNotRegex(block, r"gh api --method DELETE")

    def test_pr_pages_require_the_published_html_contract(self) -> None:
        block = _job_block("report-pr")

        self.assertIn(
            'python3 ci/check_published_html.py "$body_file" --project "$project"',
            block,
        )

    def test_pr_sticky_comment_verifier_accepts_one_marker_across_pages(self) -> None:
        run_url = "https://github.com/example/toy-projects/actions/runs/99"
        body = "\n".join(
            (
                "<!-- ici-report -->",
                f"[workflow]({run_url})",
                "[diskmap](https://pages.example/toy-projects/diskmap/pr/42/)",
                "[loglens](https://pages.example/toy-projects/loglens/pr/42/)",
                "[buildscope](https://pages.example/toy-projects/buildscope/pr/42/)",
                "[envlens](https://pages.example/toy-projects/envlens/pr/42/)",
            )
        )

        result = _run_sticky_verifier([[], [{"body": body}]])

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_pr_sticky_comment_verifier_rejects_duplicate_markers(self) -> None:
        run_url = "https://github.com/example/toy-projects/actions/runs/99"
        body = "\n".join(
            (
                "<!-- ici-report -->",
                f"[workflow]({run_url})",
                "[diskmap](https://pages.example/toy-projects/diskmap/pr/42/)",
                "[loglens](https://pages.example/toy-projects/loglens/pr/42/)",
                "[buildscope](https://pages.example/toy-projects/buildscope/pr/42/)",
                "[envlens](https://pages.example/toy-projects/envlens/pr/42/)",
            )
        )

        result = _run_sticky_verifier([[{"body": body}], [{"body": body}]])

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("sticky comment cardinality mismatch", result.stderr)

    def test_pr_sticky_comment_verifier_rejects_missing_marker_across_pages(
        self,
    ) -> None:
        result = _run_sticky_verifier(
            [
                [{"body": "ordinary discussion comment"}],
                [{"body": "unrelated report comment"}],
            ]
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("sticky comment cardinality mismatch", result.stderr)

    def test_pr_sticky_comment_verifier_rejects_multiple_markers_in_one_comment(
        self,
    ) -> None:
        run_url = "https://github.com/example/toy-projects/actions/runs/99"
        body = "\n".join(
            (
                "<!-- ici-report -->",
                "<!-- ici-report -->",
                f"[workflow]({run_url})",
                "[diskmap](https://pages.example/toy-projects/diskmap/pr/42/)",
                "[loglens](https://pages.example/toy-projects/loglens/pr/42/)",
                "[buildscope](https://pages.example/toy-projects/buildscope/pr/42/)",
                "[envlens](https://pages.example/toy-projects/envlens/pr/42/)",
            )
        )

        result = _run_sticky_verifier([[{"body": body}]])

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("sticky comment cardinality mismatch", result.stderr)

    def test_main_publisher_is_exact_main_only_and_waits_for_quality(self) -> None:
        block = _job_block("publish-main")

        self.assertIn("always() && github.event_name == 'push'", block)
        self.assertIn("github.ref == 'refs/heads/main'", block)
        self.assertIn("group: gh-pages-${{ github.repository }}", block)
        for dependency in (
            "discover",
            "verify",
            "gui-build",
            "quality-zoo-contract",
            "benchmark-smoke",
            "diskmap-benchmark-smoke",
            "buildscope-benchmark-smoke",
            "buildscope-ici-deep",
            "buildscope-python-quality",
            "envlens-python-quality",
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

        self.assertIn("quality-zoo-contract", block)
        self.assertIn('test "$QUALITY_ZOO_RESULT" = success', block)
        self.assertIn("publish-main", block)
        self.assertIn('test "$REPORT_RESULT" = success', block)
        self.assertIn('test "$MAIN_PUBLISH_RESULT" = skipped', block)
        self.assertIn('test "$EVENT_NAME" = push', block)
        self.assertIn('test "$REPORT_RESULT" = skipped', block)
        self.assertIn('test "$MAIN_PUBLISH_RESULT" = success', block)

    def test_discovery_executes_this_contract_suite(self) -> None:
        block = _job_block("discover")

        self.assertIn("ci/test_ci_workflow_contract.py", block)

    def test_quality_zoo_uses_the_pinned_release_and_uploads_evidence(self) -> None:
        block = _job_block("quality-zoo-contract")

        self.assertIn('test "$manifest_hash" = "$ICI_SHA256"', block)
        self.assertIn("sha256sum --check ici.pyz.sha256", block)
        self.assertIn("Install C++ and Qt analysis tools", block)
        self.assertIn("sudo apt-get update", block)
        self.assertIn("--no-install-recommends", block)
        for package in (
            "clang",
            "clang-tidy",
            "clazy",
            "cmake",
            "g++",
            "pkg-config",
            "qt6-base-dev",
        ):
            self.assertRegex(block, rf"(?:^|\s){re.escape(package)}(?:\s|$)")
        self.assertIn("python3 -m unittest discover -s tests -v", block)
        self.assertIn("python3 -m runner.run", block)
        self.assertIn('--ici-bin "$QUALITY_ZOO_ICI"', block)
        self.assertIn("name: quality-zoo-contract", block)

    def test_envlens_python_quality_covers_both_interpreters_and_boundaries(
        self,
    ) -> None:
        block = _job_block("envlens-python-quality")

        self.assertIn('python_version: "3.10"', block)
        self.assertIn("label: py310", block)
        self.assertIn('python_version: "3.14"', block)
        self.assertIn("label: latest", block)
        self.assertIn("-m pytest tests", block)
        self.assertIn('ruff" check src tests', block)
        self.assertIn('ruff" format --check src tests', block)
        self.assertIn('mypy" --strict --python-version 3.10 src/envlens', block)
        self.assertIn("Draft202012Validator.check_schema(schema)", block)
        self.assertGreaterEqual(
            block.count("Draft202012Validator.check_schema(schema)"), 2
        )
        self.assertIn(").validate(snapshot)", block)
        self.assertIn("--captured-at 2026-09-03T00:00:00Z", block)
        self.assertIn("uv build --out-dir", block)
        self.assertIn("SOURCE_DATE_EPOCH=1700000000", block)
        self.assertIn('cmp "$first_build/envlens-0.1.0-py3-none-any.whl"', block)
        self.assertIn('cmp "$first_build/envlens-0.1.0.tar.gz"', block)
        self.assertIn("envlens-0.1.0-py3-none-any.whl", block)
        self.assertIn('"Root-Is-Purelib": "true"', block)
        self.assertIn('"Tag": "py3-none-any"', block)
        self.assertIn("envlens wheel contains native extensions", block)
        self.assertIn("envlens sdist contains native extensions", block)
        self.assertIn('"envlens/py.typed"', block)
        self.assertIn('"$consumer_env/bin/envlens" --version', block)
        self.assertIn('wheel_schema="$artifact_dir/wheel.schema.json"', block)
        self.assertIn('files("envlens")', block)
        self.assertIn(
            'cmp "$wheel_schema" envlens/schemas/envlens-snapshot-v1.schema.json', block
        )
        self.assertIn("clean installed wheel snapshot schema validation: PASS", block)
        self.assertIn("exactly one WHEEL metadata file", block)
        self.assertIn("exactly one METADATA file", block)
        self.assertIn("exactly one PKG-INFO", block)
        self.assertIn("envlens sdist unexpectedly declares dependencies", block)

    def test_manifest_and_report_contract_are_dynamic_for_four_projects(self) -> None:
        manifest = json.loads(
            (Path(__file__).resolve().parents[1] / "ci" / "projects.json").read_text(
                encoding="utf-8"
            )
        )
        names = [entry["name"] for entry in manifest["projects"]]
        self.assertEqual(names, ["diskmap", "loglens", "buildscope", "envlens"])
        self.assertEqual(len(names), 4)

        report_block = _job_block("report-pr")
        self.assertIn(
            "PROJECT_NAMES: ${{ needs.discover.outputs.names }}", report_block
        )
        self.assertIn(
            "EXPECTED_REPORTS: ${{ needs.discover.outputs.count }}", report_block
        )
        self.assertNotIn("exact-three-project", WORKFLOW)

    def test_buildscope_release_pipeline_has_a_fixed_fail_closed_order(self) -> None:
        step_names = (
            "Inspect the release slot without mutating an existing release",
            "Create a new private draft by exact tag and commit",
            "Upload exact assets only to the new private draft",
            "Audit private draft metadata and independently downloaded assets",
            "Re-audit the fixed draft immediately before publication",
            "Publish the fully audited draft with reconciliation",
            "Independently download and audit the final public release",
            "Preserve a failed private draft for explicit review",
        )
        positions = [
            RELEASE_WORKFLOW.index(f"      - name: {name}\n") for name in step_names
        ]
        self.assertEqual(positions, sorted(positions))
        self.assertNotIn("softprops/action-gh-release", RELEASE_WORKFLOW)
        self.assertNotIn("--clobber", RELEASE_WORKFLOW)
        self.assertNotIn("gh api --method DELETE", RELEASE_WORKFLOW)

    def test_buildscope_release_selects_the_newest_exact_actions_merge_gate(
        self,
    ) -> None:
        block = _release_step_block(
            "Validate annotated tag, exact main, and green Merge Gate"
        )

        self.assertIn("actions: read", RELEASE_WORKFLOW)
        self.assertIn("gh api --paginate --slurp", block)
        self.assertIn("check-runs?per_page=100&filter=all", block)
        self.assertIn("ci/check_buildscope_merge_gate.py", block)
        self.assertIn('select "$check_runs" "$target_sha" "$GITHUB_REPOSITORY"', block)
        self.assertIn('"repos/${GITHUB_REPOSITORY}/actions/runs/${run_id}"', block)
        self.assertIn(
            'verify "$workflow_run" "$target_sha" "$GITHUB_REPOSITORY" "$run_id"',
            block,
        )
        self.assertNotIn("sort_by(.completed_at", block)

    def test_buildscope_release_slot_is_paginated_and_fail_closed(self) -> None:
        block = _release_step_block(
            "Inspect the release slot without mutating an existing release"
        )

        self.assertIn("gh api --paginate --slurp", block)
        self.assertIn('"repos/${GITHUB_REPOSITORY}/releases?per_page=100"', block)
        self.assertIn('case "$mode:$release_id" in', block)
        self.assertIn("empty:) ;;", block)
        self.assertIn("final:[1-9]*[0-9]|final:[1-9]) ;;", block)
        self.assertIn("Unsafe release slot result", block)
        self.assertNotRegex(block, r"gh api --method (POST|PATCH|DELETE)")

    def test_buildscope_release_creates_a_direct_private_draft(self) -> None:
        block = _release_step_block(
            "Create a new private draft by exact tag and commit"
        )

        self.assertIn("if: ${{ steps.release_slot.outputs.mode == 'empty' }}", block)
        self.assertIn("gh api --method POST", block)
        self.assertIn('"repos/${GITHUB_REPOSITORY}/releases"', block)
        self.assertIn('"tag_name": tag', block)
        self.assertNotIn("target_commitish", block)
        self.assertIn('"draft": True', block)
        self.assertIn('"prerelease": False', block)
        self.assertIn("--stage draft --expected-asset-count 0", block)
        self.assertIn("buildscope-release-owner:", block)
        self.assertIn("--expected-owner-marker", block)
        self.assertIn("recover-owned-draft", block)
        self.assertIn("gh api --paginate --slurp", block)
        self.assertEqual(block.count("gh api --method POST"), 1)

    def test_buildscope_release_pins_exact_draft_and_final_note_bodies(self) -> None:
        extract = _release_step_block("Extract BuildScope release notes")
        self.assertIn("buildscope-expected-final-body.md", extract)
        self.assertIn("buildscope-expected-draft-body.md", extract)
        self.assertIn('notes = Path("RELEASE_NOTES.md")', extract)
        self.assertIn('.read_text(encoding="utf-8").rstrip()', extract)
        self.assertIn('draft = f"{notes}\\n\\n{owner_marker}"', extract)

        create = _release_step_block(
            "Create a new private draft by exact tag and commit"
        )
        self.assertIn("buildscope-expected-draft-body.md", create)
        self.assertIn('"body": body', create)
        self.assertIn("draft_body_sha256=", create)
        self.assertIn('--expected-body-sha256 "$draft_body_sha256"', create)
        self.assertIn('"$owner_marker" "$draft_body_sha256"', create)

        for step_name in (
            "Upload exact assets only to the new private draft",
            "Audit private draft metadata and independently downloaded assets",
            "Re-audit the fixed draft immediately before publication",
        ):
            with self.subTest(step_name=step_name):
                block = _release_step_block(step_name)
                self.assertIn("draft_body_sha256=", block)
                self.assertIn('--expected-body-sha256 "$draft_body_sha256"', block)

        publish = _release_step_block(
            "Publish the fully audited draft with reconciliation"
        )
        self.assertIn("buildscope-expected-final-body.md", publish)
        self.assertIn('"body": body', publish)
        self.assertIn("final_body_sha256=", publish)
        self.assertIn("draft_body_sha256=", publish)
        self.assertGreaterEqual(
            publish.count('--expected-body-sha256 "$final_body_sha256"'), 2
        )
        self.assertIn('--expected-body-sha256 "$draft_body_sha256"', publish)

        final_audit = _release_step_block(
            "Independently download and audit the final public release"
        )
        self.assertIn("buildscope-expected-final-body.md", final_audit)
        self.assertGreaterEqual(
            final_audit.count('--expected-body-sha256 "$final_body_sha256"'), 4
        )

        failure = _release_step_block(
            "Preserve a failed private draft for explicit review"
        )
        self.assertIn('"$owner_marker" "$draft_body_sha256"', failure)
        self.assertIn('--expected-body-sha256 "$draft_body_sha256"', failure)
        self.assertIn('--expected-body-sha256 "$final_body_sha256"', failure)

    def test_buildscope_release_uploads_exactly_nine_assets_without_clobber(
        self,
    ) -> None:
        block = _release_step_block("Upload exact assets only to the new private draft")
        upload = re.search(
            r"for name in \\\n(?P<assets>.*?)\n\s+do",
            block,
            re.DOTALL,
        )
        if upload is None:
            self.fail("release asset loop is missing")

        assets = [
            line.strip().rstrip("\\").strip().strip('"')
            for line in upload.group("assets").splitlines()
            if line.strip()
        ]
        self.assertEqual(
            assets,
            [
                "buildscope.pyz",
                "buildscope.pyz.sha256",
                "buildscope-${VERSION}-py3-none-any.whl",
                "buildscope-${VERSION}.tar.gz",
                "buildscope-ici-deep.json",
                "buildscope-ici-deep.html",
                "buildscope-provenance.json",
                "buildscope-${VERSION}-linux-x86_64.tar.gz",
                "SHA256SUMS",
            ],
        )
        self.assertEqual(len(assets), 9)
        self.assertIn("https://uploads.github.com/repos/", block)
        self.assertIn("/releases/${RELEASE_ID}/assets?name=${name}", block)
        self.assertIn('--data-binary "@dist/$name"', block)
        self.assertIn('--header "@$auth_header"', block)
        self.assertNotIn('Authorization: Bearer $GH_TOKEN"', block)
        self.assertIn("--connect-timeout 20 --max-time 300", block)
        self.assertIn('test "$status" = 201', block)
        self.assertNotIn("gh release upload", block)
        self.assertNotIn("--clobber", block)

    def test_buildscope_release_audits_the_draft_independently(self) -> None:
        block = _release_step_block(
            "Audit private draft metadata and independently downloaded assets"
        )

        self.assertIn("gh api -H", block)
        self.assertIn('"repos/${GITHUB_REPOSITORY}/releases/${RELEASE_ID}"', block)
        self.assertIn("--stage draft", block)
        self.assertIn('--expected-release-id "$RELEASE_ID"', block)
        self.assertIn("--expected-asset-count 9", block)
        self.assertIn("ci/check_buildscope_release_assets.py", block)
        self.assertIn("ci/download_buildscope_release_assets.py", block)
        self.assertIn(
            '"$metadata" "$downloaded" "$GITHUB_REPOSITORY" "$TAG" "$VERSION"',
            block,
        )
        self.assertIn("ci/check_buildscope_release_manifest.py", block)
        self.assertIn("ci/check_buildscope_release_payload.py", block)
        self.assertNotIn('"$downloaded/buildscope.pyz" --version', block)
        self.assertIn('cmp "dist/$name" "$downloaded/$name"', block)
        self.assertIn("AUDITED_DRAFT_DIR", block)
        self.assertIn("for attempt in 1 2 3", block)

    def test_buildscope_release_reaudits_the_draft_immediately_before_publish(
        self,
    ) -> None:
        block = _release_step_block(
            "Re-audit the fixed draft immediately before publication"
        )

        self.assertIn('"repos/${GITHUB_REPOSITORY}/releases/${RELEASE_ID}"', block)
        self.assertIn("--stage draft", block)
        self.assertIn('--expected-release-id "$RELEASE_ID"', block)
        self.assertIn("--expected-asset-count 9", block)
        self.assertIn("AUDITED_DRAFT_DIR", block)
        self.assertIn("ci/check_buildscope_release_assets.py", block)
        self.assertIn("ci/check_buildscope_release_manifest.py", block)
        self.assertIn("ci/check_buildscope_release_payload.py", block)
        self.assertIn("remote_peeled", block)
        self.assertIn(
            'test "$(printf \'%s\\n\' "$remote_peeled" | awk \'{print $1}\')" = "$TARGET_SHA"',
            block,
        )

    def test_buildscope_release_publishes_with_reconciliation(self) -> None:
        block = _release_step_block(
            "Publish the fully audited draft with reconciliation"
        )

        self.assertIn("if: ${{ steps.release_slot.outputs.mode == 'empty' }}", block)
        self.assertIn("gh api --method PATCH", block)
        self.assertIn("buildscope-publish-request.json", block)
        self.assertIn('"draft": False', block)
        self.assertIn('"prerelease": False', block)
        self.assertIn('"make_latest": "true"', block)
        self.assertIn("for attempt in 1 2 3", block)
        self.assertIn("buildscope-publish-reconcile-$attempt", block)
        self.assertIn("if ! gh api -H 'Accept: application/vnd.github+json'", block)
        self.assertIn('--stage final --expected-release-id "$RELEASE_ID"', block)
        self.assertIn('--stage draft --expected-release-id "$RELEASE_ID"', block)
        self.assertIn("--expected-owner-marker", block)
        self.assertNotIn("target_commitish", block)
        self.assertIn('test "$published" = true', block)

    def test_buildscope_release_supports_existing_final_audit_only_mode(self) -> None:
        mutating_steps = (
            "Create a new private draft by exact tag and commit",
            "Upload exact assets only to the new private draft",
            "Audit private draft metadata and independently downloaded assets",
            "Re-audit the fixed draft immediately before publication",
            "Publish the fully audited draft with reconciliation",
        )
        for step_name in mutating_steps:
            with self.subTest(step_name=step_name):
                block = _release_step_block(step_name)
                self.assertIn(
                    "if: ${{ steps.release_slot.outputs.mode == 'empty' }}",
                    block,
                )

        selector = _release_step_block("Select the fixed release ID for final audit")
        self.assertIn('if [ "$MODE" = empty ]; then', selector)
        self.assertIn('elif [ "$MODE" = final ]; then', selector)
        self.assertIn('release_id="$EXISTING_ID"', selector)
        self.assertIn("Invalid final audit mode", selector)

        final_audit = _release_step_block(
            "Independently download and audit the final public release"
        )
        self.assertNotIn(
            "if: ${{ steps.release_slot.outputs.mode == 'empty' }}", final_audit
        )
        self.assertIn("MODE: ${{ steps.release_slot.outputs.mode }}", final_audit)
        self.assertIn("--stage final", final_audit)
        self.assertNotIn("--stage draft", final_audit)
        self.assertIn("ci/check_buildscope_release_state.py", final_audit)
        self.assertIn("ci/download_buildscope_release_assets.py", final_audit)
        self.assertIn("ci/check_buildscope_release_manifest.py", final_audit)
        self.assertIn("ci/check_buildscope_release_payload.py", final_audit)
        self.assertIn("for attempt in 1 2 3", final_audit)
        self.assertIn('if [ "$MODE" = empty ]; then', final_audit)
        self.assertIn('cmp "dist/$name" "$final_download/$name"', final_audit)
        self.assertIn(
            'cmp "$AUDITED_DRAFT_DIR/$name" "$final_download/$name"', final_audit
        )
        self.assertNotIn('"$downloaded/buildscope.pyz" --version', final_audit)
        self.assertIn("buildscope-final-release-post-download.json", final_audit)
        self.assertIn("buildscope-final-release-by-tag-post-download.json", final_audit)
        self.assertGreaterEqual(final_audit.count("--stage final"), 5)
        self.assertGreaterEqual(
            final_audit.count("ci/check_buildscope_release_assets.py"), 2
        )

    def test_buildscope_release_preserves_failed_draft_without_remote_mutation(
        self,
    ) -> None:
        block = _release_step_block(
            "Preserve a failed private draft for explicit review"
        )

        self.assertIn(
            "if: ${{ failure() && steps.release_slot.outputs.mode == 'empty' }}",
            block,
        )
        self.assertIn(
            "RELEASE_ID: ${{ steps.create_draft.outputs.release_id }}",
            block,
        )
        self.assertIn('if [ -z "$release_id" ]; then', block)
        self.assertIn("recover-owned-draft", block)
        self.assertIn("gh api --paginate --slurp", block)
        self.assertIn(
            'state "$candidate" "$TAG" "$VERSION" "$TARGET_SHA"',
            block,
        )
        self.assertIn('--stage draft --expected-release-id "$release_id"', block)
        self.assertIn("--expected-owner-marker", block)
        self.assertIn(
            '--stage final --expected-release-id "$release_id" --expected-asset-count 9',
            block,
        )
        self.assertIn(
            "already public; leaving it unchanged for audit-only rerun", block
        )
        self.assertIn("preserved for explicit review", block)
        self.assertIn("no remote mutation was attempted", block)
        self.assertNotIn("EXISTING_ID", block)
        self.assertNotRegex(block, r"gh api --method (POST|PATCH|DELETE)")
        self.assertNotIn("gh release delete", block)

    def test_buildscope_release_rechecks_payload_manifest_html_and_version(
        self,
    ) -> None:
        self.assertGreaterEqual(
            RELEASE_WORKFLOW.count("ci/check_buildscope_release_manifest.py"),
            3,
        )
        self.assertGreaterEqual(
            RELEASE_WORKFLOW.count("ci/check_buildscope_release_payload.py"),
            3,
        )
        self.assertIn("ci/check_published_html.py", RELEASE_WORKFLOW)
        self.assertEqual(RELEASE_WORKFLOW.count('buildscope.pyz" --version'), 2)
        self.assertNotIn('$downloaded/buildscope.pyz" --version', RELEASE_WORKFLOW)

    def test_discovery_runs_every_release_audit_regression_suite(self) -> None:
        for test_file in (
            "ci/test_check_buildscope_release_assets.py",
            "ci/test_check_buildscope_release_manifest.py",
            "ci/test_check_buildscope_merge_gate.py",
            "ci/test_check_buildscope_release_payload.py",
            "ci/test_check_buildscope_release_state.py",
            "ci/test_download_buildscope_release_assets.py",
        ):
            self.assertIn(test_file, WORKFLOW)


if __name__ == "__main__":
    unittest.main()
