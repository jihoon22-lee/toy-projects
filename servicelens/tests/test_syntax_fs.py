import os
from pathlib import Path

import pytest

from servicelens import InputError, Limits, inspect
from servicelens.fs import RootFS
from servicelens.syntax import environment_file, parse_unit, words


def test_tokenizer_quoting_and_escapes():
    assert words('one "two three" \'four five\' six\\sseven ""') == [
        "one",
        "two three",
        "four five",
        "six seven",
        "",
    ]
    assert words(r"\x41 \u0042 \101") == ["A", "B", "A"]
    with pytest.raises(InputError):
        words(r"\q")
    with pytest.raises(InputError):
        words('"unfinished')


def test_continuation_preserves_physical_line_range():
    records, errors = parse_unit(
        "[Service]\nExecStart=/bin/echo \\\n# skipped comment\n  hello\n", "/a", Limits()
    )
    assert not errors
    assert records[0]["line"] == 2 and records[0]["end_line"] == 4
    assert words(records[0]["value"]) == ["/bin/echo", "hello"]


def test_environment_multiline_and_no_shell_evaluation():
    records = environment_file(
        "A='line one\nline two'\nB=hello\\ world\nC=\"literal$HOME\\n\"\nD=a\\\nb\n",
        "/env",
        Limits(),
    )
    assert {r["name"]: r["value"] for r in records} == {
        "A": "line one\nline two",
        "B": "hello world",
        "C": "literal$HOME\\n",
        "D": "ab",
    }
    assert records[1]["line"] == 3


@pytest.mark.parametrize("body", ["A='unterminated", "A=\ufeff", "BAD NAME=x", 'A="x" junk'])
def test_invalid_environment_inputs(body):
    with pytest.raises(InputError):
        environment_file(body, "/env", Limits())


def test_rootfs_absolute_links_and_escape(image):
    root, write = image
    write("/etc/data", "inside")
    write("/link", link="/etc/data")
    write("/escape", link="../../outside")
    fs = RootFS(root, Limits())
    try:
        assert fs.read("/link") == "inside"
        with pytest.raises(InputError):
            fs.read("/escape")
    finally:
        fs.close()


def test_special_file_and_resource_limits(image):
    root, write = image
    write("/a", "12345")
    os.mkfifo(root / "fifo")
    fs = RootFS(root, Limits(file_bytes=4))
    try:
        with pytest.raises(InputError):
            fs.read("/a")
        with pytest.raises(InputError):
            fs.read("/fifo")
    finally:
        fs.close()


def test_changed_file_during_read_is_rejected(image, monkeypatch):
    root, write = image
    target = write("/a", "hello")
    original = os.read
    changed = False

    def changing(fd, count):
        nonlocal changed
        data = original(fd, count)
        if not changed:
            target.write_text("different")
            changed = True
        return data

    monkeypatch.setattr(os, "read", changing)
    fs = RootFS(root, Limits())
    try:
        with pytest.raises(InputError):
            fs.read("/a")
    finally:
        fs.close()


def test_limit_produces_report_instead_of_crash(image):
    root, write = image
    write("/usr/lib/systemd/system/app.service", "[Service]\nExecStart=/bin/true\n")
    report = inspect("app.service", root=root, limits=Limits(file_bytes=4))
    assert report["partial"]


def test_invalid_utf8_file_reports_unknown(image):
    root, write = image
    path = write("/usr/lib/systemd/system/app.service")
    path.write_bytes(b"[Service]\n\xff")
    assert inspect("app.service", root=root)["partial"]


def test_limits_and_invalid_unit_names(tmp_path: Path):
    with pytest.raises(InputError):
        Limits(files=0)
    for name in ("../a.service", "a\n.service", "a@@b.service", r"a\oops.service"):
        with pytest.raises(InputError):
            inspect(name, root=tmp_path)
