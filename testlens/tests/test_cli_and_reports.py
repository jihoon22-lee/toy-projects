import copy
import json
import os
from pathlib import Path

import pytest

from testlens.cli import main
from testlens.io import InputError, atomic_write, discover, json_text, load_json
from testlens.report import evidence_status, html_report, text_report
from testlens.validation import validate


def test_cli_installed_flow(tmp_path):
    examples = Path(__file__).parents[1] / "examples"
    before, after = tmp_path / "before.json", tmp_path / "after.json"
    for xml, output, day in [("baseline.xml", before, "01"), ("current.xml", after, "02")]:
        assert (
            main(
                [
                    "collect",
                    str(examples / xml),
                    "--project",
                    "demo",
                    "--run-id",
                    day,
                    "--executed-at",
                    f"2026-01-{day}T00:00:00Z",
                    "--complete",
                    "--output",
                    str(output),
                ]
            )
            == 0
        )
        assert main(["validate", str(output)]) == 0
    assert main(["diff", str(before), str(after), "--fail-on", "new-failure"]) == 1
    assert main(["history", str(before), str(after)]) == 0
    html = tmp_path / "report.html"
    assert (
        main(
            [
                "report",
                str(after),
                "--baseline",
                str(before),
                "--history",
                str(before),
                str(after),
                "--output",
                str(html),
            ]
        )
        == 0
    )
    assert '<script id="data"' in html.read_text()
    assert main(["diff", str(before), str(after), "--fail-on", "typo"]) == 2
    assert main(["history", str(before), "--fail-on", "new-failure"]) == 2
    assert main(["diff", str(before), str(after), "--output", str(before)]) == 2


def test_atomic_same_inode_symlink_and_hardlink(tmp_path):
    source = tmp_path / "source"
    source.write_text("original")
    link = tmp_path / "hardlink"
    os.link(source, link)
    symbolic = tmp_path / "symlink"
    symbolic.symlink_to(source)
    for dest in [source, link, symbolic]:
        with pytest.raises(InputError):
            atomic_write(dest, "replacement", [source])
    assert source.read_text() == "original"


def test_json_rejects_duplicate_and_nonfinite(tmp_path):
    path = tmp_path / "bad.json"
    for content in ['{"x":1,"x":2}', '{"x":NaN}', "[]"]:
        path.write_text(content)
        with pytest.raises(InputError):
            load_json(path)


def test_schema_and_crossfield_tampering(make_run):
    run = make_run('<testsuite><testcase name="x"/></testsuite>')
    validate(run)
    for mutate in [
        lambda d: d.update(schema="testlens.run/v99"),
        lambda d: d["summary"].update(passed=300),
        lambda d: d["tests"][0].update(status="failed"),
        lambda d: d["tests"][0]["attempts"][0].update(source_id="0" * 64),
        lambda d: d.update(executed_at="yesterday"),
    ]:
        changed = copy.deepcopy(run)
        mutate(changed)
        with pytest.raises(InputError):
            validate(changed)


def test_html_injection_and_evidence_change(make_run, tmp_path):
    run = make_run(
        '<testsuite><testcase name="&lt;/script&gt;&lt;script&gt;alert(1)&lt;/script&gt;">'
        "<failure>&lt;img src=x onerror=alert(2)&gt;</failure></testcase></testsuite>"
    )
    report = html_report(run)
    assert "</script><script>alert(1)" not in report
    assert "\\u003c/script\\u003e" in report
    payload = report.split('<script id="data" type="application/json">')[1].split("</script>")[0]
    assert json.loads(payload)["run"]["tests"][0]["name"].startswith("</script>")
    assert evidence_status(run)[0]["verification"] == "verified"
    Path(run["sources"][0]["path"]).write_text("changed")
    assert evidence_status(run)[0]["verification"] == "changed"
    Path(run["sources"][0]["path"]).unlink()
    assert evidence_status(run)[0]["verification"] == "unavailable"


def test_terminal_controls_removed(make_run):
    run = make_run('<testsuite><testcase name="x"/></testsuite>')
    run["run_id"] = "\x1b[31mred\x1b[0m\x07"
    assert "\x1b" not in text_report(run)
    assert "\x07" not in text_report(run)


def test_discovery_file_cap_and_symlinks(tmp_path):
    for i in range(3):
        (tmp_path / f"{i}.xml").write_text("<testsuite/>")
    (tmp_path / "link.xml").symlink_to(tmp_path / "0.xml")
    assert len(discover([str(tmp_path)], ".xml")) == 3
    with pytest.raises(InputError):
        discover([str(tmp_path)], ".xml", 2)


def test_report_cannot_overwrite_xml_evidence(make_run, tmp_path):
    run = make_run('<testsuite><testcase name="x"/></testsuite>')
    path = tmp_path / "run.json"
    path.write_text(json_text(run))
    xml = run["sources"][0]["path"]
    assert main(["report", str(path), "--output", xml]) == 2
    assert Path(xml).read_text().startswith("<testsuite>")


def test_json_numeric_overflow_rejected(tmp_path):
    path = tmp_path / "overflow.json"
    path.write_text('{"x":1e999}')
    with pytest.raises(InputError, match="Non-finite"):
        load_json(path)


def test_malformed_schema_type_is_clean_error():
    with pytest.raises(InputError, match="Unsupported schema"):
        validate({"schema": []})
