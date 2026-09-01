"""Tests for the fail-closed BuildScope Merge Gate identity audit."""

from __future__ import annotations

import json
import os
import sys
import unittest
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
from tempfile import TemporaryDirectory

CI_DIR = Path(__file__).resolve().parent
if str(CI_DIR) not in sys.path:
    sys.path.insert(0, str(CI_DIR))

from check_buildscope_merge_gate import (
    MAX_CHECK_RUN_PAGES,
    MAX_CHECK_RUNS_PER_PAGE,
    MAX_GITHUB_ID,
    MAX_JSON_BYTES,
    BuildScopeMergeGateError,
    MergeGateSelection,
    main,
    select_merge_gate,
    verify_merge_gate_run,
)

REPOSITORY = "jihoon22-lee/toy-projects"
TARGET_SHA = "a" * 40


class BuildScopeMergeGateTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.checks_json = self.root / "check-runs.json"
        self.run_json = self.root / "workflow-run.json"

    def _write(self, path: Path, value: object) -> None:
        path.write_text(json.dumps(value, sort_keys=True) + "\n", encoding="utf-8")

    def _check_run(
        self,
        check_run_id: int,
        *,
        workflow_run_id: int | str | None = None,
        job_id: int = 701,
        sha: str = TARGET_SHA,
        app_slug: str = "github-actions",
        status: str = "completed",
        conclusion: str | None = "success",
        repository: str = REPOSITORY,
    ) -> dict[str, object]:
        if workflow_run_id is None:
            workflow_run_id = check_run_id + 1000
        return {
            "id": check_run_id,
            "name": "Merge Gate",
            "head_sha": sha,
            "status": status,
            "conclusion": conclusion,
            "app": {"slug": app_slug},
            "details_url": (
                f"https://github.com/{repository}/actions/runs/"
                f"{workflow_run_id}/job/{job_id}"
            ),
        }

    def _write_checks(
        self, entries: list[dict[str, object]], *, total_count: int | None = None
    ) -> None:
        self._write(
            self.checks_json,
            {
                "total_count": len(entries) if total_count is None else total_count,
                "check_runs": entries,
            },
        )

    def _workflow_run(self, workflow_run_id: int = 2001) -> dict[str, object]:
        return {
            "id": workflow_run_id,
            "name": "CI Quality Gate (ici)",
            "path": ".github/workflows/ci.yml",
            "event": "push",
            "status": "completed",
            "conclusion": "success",
            "run_attempt": 1,
            "head_sha": TARGET_SHA,
            "repository": {"full_name": REPOSITORY},
            "head_repository": {"full_name": REPOSITORY},
            "html_url": (
                f"https://github.com/{REPOSITORY}/actions/runs/{workflow_run_id}"
            ),
        }

    def test_selects_highest_check_run_id_and_returns_workflow_run_identity(
        self,
    ) -> None:
        self._write_checks(
            [
                self._check_run(100, workflow_run_id=2000),
                self._check_run(101, workflow_run_id=2001),
            ]
        )

        selection = select_merge_gate(self.checks_json, TARGET_SHA, REPOSITORY)

        self.assertEqual(
            selection,
            MergeGateSelection(
                check_run_id=101,
                workflow_run_id=2001,
                details_url=(
                    f"https://github.com/{REPOSITORY}/actions/runs/2001/job/701"
                ),
            ),
        )

    def test_newer_pending_run_refuses_fallback_to_older_success(self) -> None:
        self._write_checks(
            [
                self._check_run(100, workflow_run_id=2000),
                self._check_run(
                    101,
                    workflow_run_id=2001,
                    status="in_progress",
                    conclusion=None,
                ),
            ]
        )

        with self.assertRaisesRegex(BuildScopeMergeGateError, "newest exact"):
            select_merge_gate(self.checks_json, TARGET_SHA, REPOSITORY)

    def test_newer_failure_run_refuses_fallback_to_older_success(self) -> None:
        self._write_checks(
            [
                self._check_run(100, workflow_run_id=2000),
                self._check_run(
                    101,
                    workflow_run_id=2001,
                    status="completed",
                    conclusion="failure",
                ),
            ]
        )

        with self.assertRaisesRegex(BuildScopeMergeGateError, "newest exact"):
            select_merge_gate(self.checks_json, TARGET_SHA, REPOSITORY)

    def test_spoofed_app_is_not_an_eligible_merge_gate(self) -> None:
        self._write_checks(
            [
                self._check_run(999, workflow_run_id=2999, app_slug="evil-app"),
                self._check_run(100, workflow_run_id=2000),
            ]
        )

        selection = select_merge_gate(self.checks_json, TARGET_SHA, REPOSITORY)

        self.assertEqual(selection.check_run_id, 100)
        self.assertEqual(selection.workflow_run_id, 2000)

        self._write_checks(
            [self._check_run(999, workflow_run_id=2999, app_slug="evil-app")]
        )
        with self.assertRaisesRegex(BuildScopeMergeGateError, "no exact"):
            select_merge_gate(self.checks_json, TARGET_SHA, REPOSITORY)

    def test_wrong_sha_is_not_an_eligible_merge_gate(self) -> None:
        self._write_checks([self._check_run(100, sha="b" * 40)])

        with self.assertRaisesRegex(BuildScopeMergeGateError, "no exact"):
            select_merge_gate(self.checks_json, TARGET_SHA, REPOSITORY)

    def test_total_count_must_equal_the_fetched_page(self) -> None:
        self._write_checks([self._check_run(100)], total_count=2)

        with self.assertRaisesRegex(BuildScopeMergeGateError, "incomplete"):
            select_merge_gate(self.checks_json, TARGET_SHA, REPOSITORY)

    def test_select_accepts_a_complete_paginated_slurp_response(self) -> None:
        first_page = {
            "total_count": 2,
            "check_runs": [self._check_run(100, workflow_run_id=2000)],
        }
        second_page = {
            "total_count": 2,
            "check_runs": [self._check_run(101, workflow_run_id=2001)],
        }
        self._write(self.checks_json, [first_page, second_page])

        selection = select_merge_gate(self.checks_json, TARGET_SHA, REPOSITORY)

        self.assertEqual(selection.check_run_id, 101)
        self.assertEqual(selection.workflow_run_id, 2001)

    def test_paginated_response_rejects_hidden_or_incomplete_entries(self) -> None:
        self._write(
            self.checks_json,
            [
                {
                    "total_count": 2,
                    "check_runs": [self._check_run(100)],
                }
            ],
        )
        with self.assertRaisesRegex(BuildScopeMergeGateError, "incomplete"):
            select_merge_gate(self.checks_json, TARGET_SHA, REPOSITORY)

        self._write(
            self.checks_json,
            [
                {
                    "total_count": 2,
                    "check_runs": [self._check_run(100)],
                },
                {
                    "total_count": 2,
                    "check_runs": [],
                },
            ],
        )
        with self.assertRaisesRegex(BuildScopeMergeGateError, "incomplete"):
            select_merge_gate(self.checks_json, TARGET_SHA, REPOSITORY)

    def test_paginated_response_rejects_inconsistent_counts_duplicates_and_limits(
        self,
    ) -> None:
        self._write(
            self.checks_json,
            [
                {
                    "total_count": 2,
                    "check_runs": [self._check_run(100)],
                },
                {
                    "total_count": 3,
                    "check_runs": [self._check_run(101), self._check_run(102)],
                },
            ],
        )
        with self.assertRaisesRegex(BuildScopeMergeGateError, "disagree"):
            select_merge_gate(self.checks_json, TARGET_SHA, REPOSITORY)

        duplicate = self._check_run(100)
        self._write(
            self.checks_json,
            [
                {"total_count": 2, "check_runs": [duplicate]},
                {"total_count": 2, "check_runs": [duplicate]},
            ],
        )
        with self.assertRaisesRegex(BuildScopeMergeGateError, "duplicate"):
            select_merge_gate(self.checks_json, TARGET_SHA, REPOSITORY)

        too_many_entries = [self._check_run(index + 1) for index in range(101)]
        self._write(
            self.checks_json,
            [{"total_count": 101, "check_runs": too_many_entries}],
        )
        with self.assertRaisesRegex(BuildScopeMergeGateError, "page size"):
            select_merge_gate(self.checks_json, TARGET_SHA, REPOSITORY)

        pages = [
            {"total_count": 0, "check_runs": []} for _ in range(MAX_CHECK_RUN_PAGES + 1)
        ]
        self._write(self.checks_json, pages)
        with self.assertRaisesRegex(BuildScopeMergeGateError, "1 to"):
            select_merge_gate(self.checks_json, TARGET_SHA, REPOSITORY)

        one_page_too_large = [
            {"total_count": 0, "check_runs": []}
            for _ in range(MAX_CHECK_RUNS_PER_PAGE + 1)
        ]
        self._write(self.checks_json, one_page_too_large)
        with self.assertRaisesRegex(BuildScopeMergeGateError, "1 to"):
            select_merge_gate(self.checks_json, TARGET_SHA, REPOSITORY)

    def test_rejects_invalid_details_url_and_duplicate_check_run_id(self) -> None:
        invalid = self._check_run(100)
        invalid["details_url"] = (
            f"https://github.com/{REPOSITORY}/actions/runs/2000/job/0"
        )
        self._write_checks([invalid])
        with self.assertRaisesRegex(BuildScopeMergeGateError, "details_url"):
            select_merge_gate(self.checks_json, TARGET_SHA, REPOSITORY)

        self._write_checks(
            [
                self._check_run(100, workflow_run_id=2000),
                self._check_run(100, workflow_run_id=2001),
            ]
        )
        with self.assertRaisesRegex(BuildScopeMergeGateError, "duplicate"):
            select_merge_gate(self.checks_json, TARGET_SHA, REPOSITORY)

    def test_rejects_malformed_oversized_and_duplicate_json(self) -> None:
        malformed_values: tuple[object, ...] = (
            [],
            {"total_count": 1},
            {"total_count": 1, "check_runs": [None]},
        )
        for value in malformed_values:
            with self.subTest(value=value):
                self._write(self.checks_json, value)
                with self.assertRaises(BuildScopeMergeGateError):
                    select_merge_gate(self.checks_json, TARGET_SHA, REPOSITORY)

        self.checks_json.write_text(
            '{"total_count": 0, "total_count": 0, "check_runs": []}',
            encoding="utf-8",
        )
        with self.assertRaisesRegex(BuildScopeMergeGateError, "valid JSON"):
            select_merge_gate(self.checks_json, TARGET_SHA, REPOSITORY)

        self.checks_json.touch()
        os.truncate(self.checks_json, MAX_JSON_BYTES + 1)
        with self.assertRaisesRegex(BuildScopeMergeGateError, "outside"):
            select_merge_gate(self.checks_json, TARGET_SHA, REPOSITORY)

    def test_rejects_unsafe_symlink_and_fifo_inputs(self) -> None:
        if not hasattr(os, "O_NOFOLLOW"):
            self.skipTest("O_NOFOLLOW is unavailable")
        source = self.root / "source.json"
        self._write(source, {"total_count": 0, "check_runs": []})
        symlink = self.root / "symlink.json"
        symlink.symlink_to(source)
        with self.assertRaises(BuildScopeMergeGateError):
            select_merge_gate(symlink, TARGET_SHA, REPOSITORY)

        fifo = self.root / "runs.fifo"
        os.mkfifo(fifo)
        with self.assertRaises(BuildScopeMergeGateError):
            select_merge_gate(fifo, TARGET_SHA, REPOSITORY)

    def test_verify_accepts_exact_actions_workflow_run(self) -> None:
        self._write(self.run_json, self._workflow_run())

        html_url = verify_merge_gate_run(
            self.run_json,
            TARGET_SHA,
            REPOSITORY,
            2001,
        )

        self.assertEqual(
            html_url,
            f"https://github.com/{REPOSITORY}/actions/runs/2001",
        )

    def test_verify_rejects_wrong_identity_and_workflow_fields(self) -> None:
        field_cases: tuple[tuple[str, object, str], ...] = (
            ("id", 2002, "workflow run id"),
            ("repository", {"full_name": "other/repo"}, "repository"),
            ("head_repository", {"full_name": "other/repo"}, "head repository"),
            ("head_sha", "b" * 40, "head SHA"),
            ("name", "Other workflow", "name"),
            ("path", ".github/workflows/other.yml", "path"),
            ("event", "pull_request", "event"),
            ("status", "in_progress", "status"),
            ("conclusion", "failure", "conclusion"),
            ("run_attempt", 0, "attempt"),
            (
                "html_url",
                f"https://github.com/{REPOSITORY}/actions/runs/2001?x=1",
                "html_url",
            ),
        )
        for field, replacement, message in field_cases:
            with self.subTest(field=field):
                payload = self._workflow_run()
                payload[field] = replacement
                self._write(self.run_json, payload)
                with self.assertRaisesRegex(BuildScopeMergeGateError, message):
                    verify_merge_gate_run(
                        self.run_json,
                        TARGET_SHA,
                        REPOSITORY,
                        2001,
                    )

    def test_verify_rejects_malformed_oversized_and_duplicate_json(self) -> None:
        self._write(self.run_json, [])
        with self.assertRaises(BuildScopeMergeGateError):
            verify_merge_gate_run(self.run_json, TARGET_SHA, REPOSITORY, 2001)

        self.run_json.write_text(
            '{"id": 2001, "id": 2002}',
            encoding="utf-8",
        )
        with self.assertRaisesRegex(BuildScopeMergeGateError, "valid JSON"):
            verify_merge_gate_run(self.run_json, TARGET_SHA, REPOSITORY, 2001)

        self.run_json.touch()
        os.truncate(self.run_json, MAX_JSON_BYTES + 1)
        with self.assertRaisesRegex(BuildScopeMergeGateError, "outside"):
            verify_merge_gate_run(self.run_json, TARGET_SHA, REPOSITORY, 2001)

    def test_rejects_invalid_expected_ids_and_repository_inputs(self) -> None:
        self._write(self.run_json, self._workflow_run())
        for run_id in (0, -1, True, MAX_GITHUB_ID + 1):
            with (
                self.subTest(run_id=run_id),
                self.assertRaises(BuildScopeMergeGateError),
            ):
                verify_merge_gate_run(
                    self.run_json,
                    TARGET_SHA,
                    REPOSITORY,
                    run_id,  # type: ignore[arg-type]
                )

        for repository in ("owner", "owner/other/path", "owner name/repo"):
            with (
                self.subTest(repository=repository),
                self.assertRaises(BuildScopeMergeGateError),
            ):
                verify_merge_gate_run(
                    self.run_json,
                    TARGET_SHA,
                    repository,
                    2001,
                )

    def test_cli_select_outputs_only_workflow_run_id_and_details_url(self) -> None:
        self._write_checks([self._check_run(100, workflow_run_id=2000)])
        output = StringIO()
        with redirect_stdout(output):
            self.assertEqual(
                main(["select", str(self.checks_json), TARGET_SHA, REPOSITORY]),
                0,
            )

        self.assertEqual(
            json.loads(output.getvalue()),
            {
                "details_url": (
                    f"https://github.com/{REPOSITORY}/actions/runs/2000/job/701"
                ),
                "workflow_run_id": 2000,
            },
        )

    def test_cli_verify_returns_success_only_after_full_run_audit(self) -> None:
        self._write(self.run_json, self._workflow_run())
        output = StringIO()
        with redirect_stdout(output):
            self.assertEqual(
                main(
                    [
                        "verify",
                        str(self.run_json),
                        TARGET_SHA,
                        REPOSITORY,
                        "2001",
                    ]
                ),
                0,
            )
        self.assertEqual(
            json.loads(output.getvalue()),
            {
                "html_url": f"https://github.com/{REPOSITORY}/actions/runs/2001",
                "workflow_run_id": 2001,
            },
        )

    def test_cli_failure_has_no_success_output(self) -> None:
        self._write_checks(
            [
                self._check_run(
                    100,
                    status="in_progress",
                    conclusion=None,
                )
            ]
        )
        output = StringIO()
        error = StringIO()
        with (
            redirect_stdout(output),
            redirect_stderr(error),
            self.assertRaises(SystemExit) as raised,
        ):
            main(["select", str(self.checks_json), TARGET_SHA, REPOSITORY])

        self.assertEqual(raised.exception.code, 1)
        self.assertEqual(output.getvalue(), "")
        self.assertIn("audit failed", error.getvalue())


if __name__ == "__main__":
    unittest.main()
