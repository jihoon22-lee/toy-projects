from pathlib import Path

import pytest

from testlens.adapters import Limits, parse_tree
from testlens.core import collect
from testlens.io import InputError
from testlens.validation import validate


@pytest.mark.parametrize(
    "filename,dialect,count",
    [
        ("qt-diskmap.xml", "qt", 3),
        ("qt-buildscope.xml", "qt", 7),
        ("qt-loglens.xml", "qt", 3),
        ("pytest-envlens.xml", "pytest", 7),
        ("ctest-repo.xml", "ctest-junit", 3),
        ("ctest-dashboard.xml", "ctest", 3),
    ],
)
def test_real_runner_fixtures(fixture_dir, filename, dialect, count):
    run = collect(
        [fixture_dir / filename],
        project="repository",
        run_id="fixture",
        dialect=dialect,
        declared_complete=True,
    )
    validate(run)
    assert run["complete"]
    assert len(run["tests"]) >= count
    assert all(t["status"] == "passed" for t in run["tests"])
    expected = "runner-target" if dialect.startswith("ctest") else "test-case"
    assert {t["granularity"] for t in run["tests"]} == {expected}


@pytest.mark.parametrize(
    "xml",
    [
        '<!DOCTYPE a [<!ENTITY x "boom">]><testsuite name="a">&x;</testsuite>',
        '<!DOCTYPE a SYSTEM "file:///etc/passwd"><testsuite/>',
        '<!DOCTYPE a [<!ENTITY x SYSTEM "https://example.com">]><testsuite>&x;</testsuite>',
        "<testsuite><testcase>",
        "<root/>",
    ],
)
def test_unsafe_or_unsupported(make_run, xml):
    with pytest.raises(InputError):
        make_run(xml)


@pytest.mark.parametrize(
    "limit,xml",
    [
        (Limits(depth=2), b"<testsuite><testcase><failure/></testcase></testsuite>"),
        (Limits(nodes=2), b"<testsuite><testcase/><testcase/></testsuite>"),
    ],
)
def test_structural_limits(limit, xml):
    with pytest.raises(InputError, match="budget"):
        parse_tree(xml, limit)


def test_case_file_and_total_limits(make_run):
    xml = '<testsuite><testcase name="a"/><testcase name="b"/></testsuite>'
    for limit in [Limits(file_bytes=10), Limits(total_bytes=10), Limits(cases=1)]:
        with pytest.raises(InputError):
            make_run(xml, limits=limit)


def test_output_truncation(make_run):
    run = make_run(
        '<testsuite><testcase name="a"><failure message="oops">abcdef</failure>'
        "<system-out>abcdef</system-out></testcase></testsuite>",
        limits=Limits(output_chars=3),
    )
    attempt = run["tests"][0]["attempts"][0]
    assert attempt["output"] == {"text": "abc", "truncated": True}
    assert attempt["messages"][0]["text"] == "abc"
    assert attempt["messages"][0]["truncated"]


def test_states_and_duration(make_run):
    run = make_run("""<testsuite tests="6"><testcase name="pass" time="NaN"/>
    <testcase name="fail"><failure message="bad"/></testcase>
    <testcase name="error"><error/></testcase>
    <testcase name="skip"><skipped type="pytest.xfail"/></testcase>
    <testcase name="disabled" status="notrun"/>
    <testcase name="retry"><flakyFailure>bad</flakyFailure></testcase></testsuite>""")
    states = {t["name"]: t["status"] for t in run["tests"]}
    assert states == {
        "pass": "passed",
        "fail": "failed",
        "error": "error",
        "skip": "skipped",
        "disabled": "not-run",
        "retry": "unknown",
    }
    assert not run["complete"]
    skip = next(t for t in run["tests"] if t["name"] == "skip")
    assert skip["attempts"][0]["expected_outcome"] == "xfail"


def test_incomplete_suite_and_declared_count(make_run):
    run = make_run(
        '<testsuite name="x" tests="3"><error>setup failed</error>'
        '<testcase name="one"/></testsuite>'
    )
    assert not run["complete"]
    assert {d["code"] for d in run["diagnostics"]} == {"count-mismatch", "suite-error"}


def test_partial_preserves_only_preceding_files(tmp_path):
    good = tmp_path / "a.xml"
    bad = tmp_path / "b.xml"
    good.write_text('<testsuite><testcase name="ok"/></testsuite>')
    bad.write_text("<testsuite><testcase")
    run = collect(
        [good, bad], project="a", run_id="one", allow_partial=True, declared_complete=True
    )
    assert len(run["tests"]) == 1
    assert not run["complete"]
    validate(run)


def test_ctest_notrun_timeout_and_output(make_run):
    run = make_run(
        """<Site><Testing><Test Status="notrun"><Name>disabled</Name></Test>
    <Test Status="failed"><Name>timeout</Name><Results><NamedMeasurement name="Exit Code">
    <Value>Timeout</Value></NamedMeasurement>
    <NamedMeasurement name="Execution Time"><Value>2.0</Value>
    </NamedMeasurement><Measurement><Value>evidence</Value></Measurement></Results></Test>
    <EndDateTime>today</EndDateTime></Testing></Site>""",
        dialect="ctest",
    )
    assert run["summary"] == {"not-run": 1, "failed": 1}
    timeout = next(t for t in run["tests"] if t["name"] == "timeout")
    assert timeout["duration_seconds"] == 2
    assert timeout["attempts"][0]["output"]["text"] == "evidence"


def test_dialect_mismatch_is_error(make_run):
    with pytest.raises(InputError):
        make_run("<testsuite/>", dialect="ctest")
    with pytest.raises(InputError):
        make_run("<Site/>", dialect="qt")


def test_duplicate_content_not_counted_twice(tmp_path):
    one, two = tmp_path / "one.xml", tmp_path / "two.xml"
    for p in (one, two):
        p.write_text('<testsuite><testcase name="once"/></testsuite>')
    run = collect([one, two], project="p", run_id="r", declared_complete=True)
    assert len(run["tests"]) == len(run["sources"]) == 1
    assert run["diagnostics"][0]["code"] == "duplicate-input"
    assert run["complete"]


def test_unicode_parameter_names_and_namespace(make_run):
    run = make_run(
        '<testsuite xmlns="urn:junit" name="테스트"><testcase name="test[한글/α]"/>'
        '<testcase name="test[한글/β]"/></testsuite>'
    )
    assert len({t["id"] for t in run["tests"]}) == 2
    assert all("한글" in t["name"] for t in run["tests"])


def test_source_paths_do_not_affect_identity(make_run, tmp_path):
    a = make_run('<testsuite name="s"><testcase name="x" file="/old/root/test.py"/></testsuite>')
    b = make_run(
        '<testsuite name="s"><testcase name="x" file="/new/root/test.py"/></testsuite>',
        root=Path("/new/root"),
    )
    assert a["tests"][0]["id"] == b["tests"][0]["id"]
    assert b["tests"][0]["attempts"][0]["source_file_normalized"] == "test.py"


def test_unrepresented_failure_cannot_claim_complete(make_run):
    run = make_run('<testsuite failures="1"><testcase name="appears_passed"/></testsuite>')
    assert not run["complete"]
    assert run["diagnostics"][0]["code"] == "count-mismatch"


def test_duplicate_attempt_property_rejected(make_run):
    with pytest.raises(InputError, match="Duplicate property"):
        make_run(
            '<testsuite><testcase name="x"><properties>'
            '<property name="testlens.attempt" value="1"/>'
            '<property name="testlens.attempt" value="2"/>'
            "</properties></testcase></testsuite>"
        )


def test_unknown_status_marks_incomplete(make_run):
    run = make_run('<testsuite><testcase name="x" status="interrupted"/></testsuite>')
    assert run["tests"][0]["status"] == "unknown"
    assert not run["complete"]
