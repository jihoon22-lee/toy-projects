#!/usr/bin/env python3
"""Validate an ici HTML report's title and Zero-CDN resource contract."""

from __future__ import annotations

import argparse
import os
import re
import stat
from html.parser import HTMLParser
from pathlib import Path

MAX_HTML_BYTES = 20_000_000
_ABSOLUTE_URL = re.compile(r"(?:^|[\s,])(?:https?:)?//", re.IGNORECASE)
_CSS_EXTERNAL = re.compile(
    r"(?:url\(\s*['\"]?(?:https?:)?//|@import\s+(?:url\()?\s*['\"]?(?:https?:)?//)",
    re.IGNORECASE,
)
_RESOURCE_ATTRIBUTES = {
    "audio": ("src",),
    "base": ("href",),
    "embed": ("src",),
    "form": ("action",),
    "iframe": ("src",),
    "image": ("href", "xlink:href"),
    "img": ("src", "srcset"),
    "input": ("src",),
    "link": ("href",),
    "object": ("data",),
    "script": ("src",),
    "source": ("src", "srcset"),
    "track": ("src",),
    "use": ("href", "xlink:href"),
    "video": ("poster", "src"),
}


class PublishedHtmlError(ValueError):
    """The downloaded report violates the trusted Pages contract."""


def _stat_signature(info: os.stat_result) -> tuple[int, ...]:
    return (
        info.st_dev,
        info.st_ino,
        stat.S_IFMT(info.st_mode),
        info.st_size,
        info.st_mtime_ns,
        info.st_ctime_ns,
    )


class _ReportParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.doctype_html = False
        self.external_resources: list[str] = []
        self._in_style = False
        self._in_title = False
        self._style_parts: list[str] = []
        self._title_parts: list[str] = []
        self.title_count = 0

    @property
    def title(self) -> str:
        return "".join(self._title_parts).strip()

    @property
    def style_text(self) -> str:
        return "".join(self._style_parts)

    def handle_decl(self, decl: str) -> None:
        if decl.strip().lower() == "doctype html":
            self.doctype_html = True

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        lowered = tag.lower()
        values = {name.lower(): value or "" for name, value in attrs}
        if lowered == "title":
            self.title_count += 1
            self._in_title = True
        elif lowered == "style":
            self._in_style = True

        style = values.get("style", "")
        if style and _CSS_EXTERNAL.search(style):
            self.external_resources.append(f"{lowered}[style]")
        for attribute in _RESOURCE_ATTRIBUTES.get(lowered, ()):
            value = values.get(attribute, "")
            if value and _ABSOLUTE_URL.search(value):
                self.external_resources.append(f"{lowered}[{attribute}]={value}")

    def handle_endtag(self, tag: str) -> None:
        lowered = tag.lower()
        if lowered == "title":
            self._in_title = False
        elif lowered == "style":
            self._in_style = False

    def handle_data(self, data: str) -> None:
        if self._in_title:
            self._title_parts.append(data)
        if self._in_style:
            self._style_parts.append(data)


def check_report(path: Path, project: str) -> None:
    """Raise ``PublishedHtmlError`` unless ``path`` is the expected offline report."""

    if not project or project in {".", ".."} or "/" in project or "\\" in project:
        raise PublishedHtmlError(f"invalid project label: {project!r}")
    nofollow = getattr(os, "O_NOFOLLOW", None)
    if nofollow is None:
        raise PublishedHtmlError("this platform cannot safely refuse report symlinks")
    flags = os.O_RDONLY
    flags |= getattr(os, "O_CLOEXEC", 0)
    flags |= nofollow
    flags |= getattr(os, "O_NONBLOCK", 0)
    try:
        descriptor = os.open(path, flags)
    except OSError as exc:
        raise PublishedHtmlError(f"cannot open report safely: {exc}") from exc
    try:
        opened = os.fstat(descriptor)
        if not stat.S_ISREG(opened.st_mode):
            raise PublishedHtmlError("report must be a regular file")
        if opened.st_size <= 0 or opened.st_size > MAX_HTML_BYTES:
            raise PublishedHtmlError(
                f"report size is outside the accepted range: {opened.st_size}"
            )
        with os.fdopen(descriptor, "rb", closefd=True) as stream:
            descriptor = -1
            raw_payload = stream.read(MAX_HTML_BYTES + 1)
            after = os.fstat(stream.fileno())
        try:
            named = path.stat(follow_symlinks=False)
        except OSError as exc:
            raise PublishedHtmlError(f"report path cannot be rechecked: {exc}") from exc
        if _stat_signature(opened) != _stat_signature(after) or _stat_signature(
            opened
        ) != _stat_signature(named):
            raise PublishedHtmlError("report changed while it was read")
    except PublishedHtmlError:
        raise
    except OSError as exc:
        raise PublishedHtmlError(f"cannot read report safely: {exc}") from exc
    finally:
        if descriptor >= 0:
            os.close(descriptor)
    if len(raw_payload) > MAX_HTML_BYTES:
        raise PublishedHtmlError("report exceeds the accepted read bound")
    try:
        payload = raw_payload.decode("utf-8")
    except UnicodeError as exc:
        raise PublishedHtmlError(f"cannot read report as UTF-8: {exc}") from exc

    parser = _ReportParser()
    parser.feed(payload)
    parser.close()

    expected_title = f"ici Verification Report — {project}"
    if not parser.doctype_html:
        raise PublishedHtmlError("report is missing <!doctype html>")
    if parser.title_count != 1 or parser.title != expected_title:
        raise PublishedHtmlError(
            f"unexpected report title: expected={expected_title!r} actual={parser.title!r}"
        )
    if _CSS_EXTERNAL.search(parser.style_text):
        parser.external_resources.append("style block")
    if parser.external_resources:
        raise PublishedHtmlError(
            "external resource references are forbidden: "
            + ", ".join(parser.external_resources)
        )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--project", required=True)
    args = parser.parse_args(argv)
    try:
        check_report(args.report, args.project)
    except PublishedHtmlError as exc:
        parser.exit(1, f"published HTML contract failed: {exc}{os.linesep}")
    print(f"published HTML contract passed: {args.project} ({args.report})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
