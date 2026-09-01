"""Tests for the trusted Pages HTML contract checker."""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch

CI_DIR = Path(__file__).resolve().parent
if str(CI_DIR) not in sys.path:
    sys.path.insert(0, str(CI_DIR))

from check_published_html import PublishedHtmlError, check_report


def _html(project: str, body: str = "") -> str:
    return (
        "<!doctype html><html><head>"
        f"<title>ici Verification Report — {project}</title>"
        "<style>body{background:linear-gradient(#fff,#eee)}</style>"
        "</head><body>"
        f'{body}<a href="https://github.com/example/project">source</a>'
        "</body></html>"
    )


class PublishedHtmlTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.report = self.root / "index.html"

    def test_accepts_inline_report_and_external_navigation_link(self) -> None:
        self.report.write_text(
            _html("buildscope", '<svg><use href="#status"></use></svg>'),
            encoding="utf-8",
        )

        check_report(self.report, "buildscope")

    def test_rejects_wrong_or_duplicate_title(self) -> None:
        self.report.write_text(_html("diskmap"), encoding="utf-8")
        with self.assertRaisesRegex(PublishedHtmlError, "unexpected report title"):
            check_report(self.report, "buildscope")

        self.report.write_text(
            _html("buildscope").replace("</head>", "<title>duplicate</title></head>"),
            encoding="utf-8",
        )
        with self.assertRaisesRegex(PublishedHtmlError, "unexpected report title"):
            check_report(self.report, "buildscope")

    def test_rejects_external_resource_attributes(self) -> None:
        for resource in (
            '<script src="https://cdn.example/app.js"></script>',
            '<link rel="stylesheet" href="//cdn.example/app.css">',
            '<img srcset="local.png 1x, https://cdn.example/remote.png 2x">',
            '<svg><use href="https://cdn.example/icons.svg#ok"></use></svg>',
        ):
            with self.subTest(resource=resource):
                self.report.write_text(_html("loglens", resource), encoding="utf-8")
                with self.assertRaisesRegex(PublishedHtmlError, "external resource"):
                    check_report(self.report, "loglens")

    def test_rejects_external_css_resources(self) -> None:
        for resource in (
            '<div style="background:url(https://cdn.example/bg.png)"></div>',
            '<style>@import "//cdn.example/theme.css";</style>',
        ):
            with self.subTest(resource=resource):
                self.report.write_text(_html("diskmap", resource), encoding="utf-8")
                with self.assertRaisesRegex(PublishedHtmlError, "external resource"):
                    check_report(self.report, "diskmap")

    def test_rejects_non_regular_and_invalid_project_inputs(self) -> None:
        directory = self.root / "directory"
        directory.mkdir()
        with self.assertRaisesRegex(PublishedHtmlError, "regular file"):
            check_report(directory, "buildscope")

        self.report.write_text(_html("buildscope"), encoding="utf-8")
        with self.assertRaisesRegex(PublishedHtmlError, "invalid project"):
            check_report(self.report, "../buildscope")

    def test_rejects_symlink_fifo_invalid_utf8_and_oversized_reports(self) -> None:
        target = self.root / "target.html"
        target.write_text(_html("buildscope"), encoding="utf-8")
        self.report.symlink_to(target)
        with self.assertRaisesRegex(PublishedHtmlError, "safely"):
            check_report(self.report, "buildscope")

        self.report.unlink()
        os.mkfifo(self.report, 0o600)
        with self.assertRaisesRegex(PublishedHtmlError, "regular file"):
            check_report(self.report, "buildscope")

        self.report.unlink()
        self.report.write_bytes(b"\xff")
        with self.assertRaisesRegex(PublishedHtmlError, "UTF-8"):
            check_report(self.report, "buildscope")

        self.report.write_bytes(b"x" * 32)
        with (
            patch("check_published_html.MAX_HTML_BYTES", 16),
            self.assertRaisesRegex(PublishedHtmlError, "accepted range"),
        ):
            check_report(self.report, "buildscope")


if __name__ == "__main__":
    unittest.main()
