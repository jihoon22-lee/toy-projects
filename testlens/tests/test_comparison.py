import copy

import pytest

from testlens.core import compare, history, policy_violations
from testlens.io import InputError
from testlens.validation import validate


def test_all_transitions_and_timing(make_run):
    before = make_run("""<testsuite name="s"><testcase name="regress" time="1"/>
    <testcase name="recover"><failure/></testcase><testcase name="removed"/>
    <testcase name="skip"><failure/></testcase><testcase name="persist"><error/></testcase>
    <testcase name="zero" time="0"/></testsuite>""")
    after = make_run("""<testsuite name="s">
    <testcase name="regress" time="1.5"><failure/></testcase>
    <testcase name="recover"/><testcase name="new"><error/></testcase>
    <testcase name="skip"><skipped/></testcase><testcase name="persist"><failure/></testcase>
    <testcase name="zero" time="1"/></testsuite>""")
    result = compare(before, after)
    validate(result)
    byname = {c["name"]: c for c in result["changes"]}
    assert byname["regress"]["kind"] == "new-failure"
    assert byname["regress"]["slowdown"]
    assert byname["recover"]["kind"] == "recovered"
    assert byname["removed"]["kind"] == "missing"
    assert byname["new"]["kind"] == "new-test-failure"
    assert byname["skip"]["kind"] == "status-changed"
    assert byname["persist"]["kind"] == "persistent-failure"
    assert byname["zero"]["relative_change"] is None
    assert not byname["zero"]["slowdown"]
    assert policy_violations(result, {"new-failure", "slowdown"}) == ["new-failure", "slowdown"]


def test_identity_and_aliases(make_run):
    first = make_run('<testsuite name="s"><testcase name="old"/></testsuite>')
    second = make_run('<testsuite name="s"><testcase name="new"/></testsuite>')
    old = first["tests"][0]["id"]
    new = second["tests"][0]["id"]
    result = compare(first, second, aliases={old: new})
    assert result["changes"][0]["alias_from"] == old
    assert result["changes"][0]["kind"] == "unchanged"
    with pytest.raises(InputError):
        compare(first, second, aliases={"missing": new})


def test_retries_require_explicit_contiguous_attempts(make_run):
    run = make_run("""<testsuite><testcase name="x"><failure/><properties>
    <property name="testlens.attempt" value="1"/></properties></testcase>
    <testcase name="x"><properties><property name="testlens.attempt" value="2"/>
    </properties></testcase></testsuite>""")
    assert run["tests"][0]["status"] == "passed"
    assert run["tests"][0]["any_failure"]
    assert len(run["tests"][0]["attempts"]) == 2
    validate(run)
    summary = history([run])
    validate(summary)
    assert summary["tests"][0]["failure_count"] == 0
    assert summary["tests"][0]["any_failure_count"] == 1


@pytest.mark.parametrize(
    "properties", ["", '<properties><property name="testlens.attempt" value="2"/></properties>']
)
def test_duplicate_or_incomplete_retry_is_unknown(make_run, properties):
    run = make_run(
        '<testsuite><testcase name="x">' + properties + "</testcase>"
        '<testcase name="x">' + properties + "</testcase></testsuite>"
    )
    assert run["tests"][0]["status"] == "unknown"
    assert not run["complete"]


def test_shards_and_manifest(make_run):
    run = make_run(
        '<testsuite><properties><property name="testlens.shard" value="1"/>'
        '</properties><testcase name="x"/></testsuite>',
        expected_shards=["1", "2"],
    )
    assert not run["complete"]
    assert run["observed_shards"] == ["1"]
    second = make_run('<testsuite><testcase name="x"/></testsuite>', manifest={"test_ids": []})
    assert not second["complete"]
    assert second["diagnostics"][0]["code"] == "unexpected-test"


def test_history_denominator_dedup_and_order(make_run):
    passed = make_run('<testsuite><testcase name="x"/></testsuite>')
    fail = make_run('<testsuite><testcase name="x"><failure/></testcase></testsuite>')
    skip = make_run('<testsuite><testcase name="x"><skipped/></testcase></testsuite>')
    result = history([skip, passed, fail, passed], last=3)
    validate(result)
    assert result["run_ids"] == [passed["run_id"], fail["run_id"], skip["run_id"]]
    assert result["tests"][0]["failure_rate"] == 0.5
    assert result["tests"][0]["excluded_count"] == 1
    result = history([passed, fail, skip], last=2)
    assert result["tests"][0]["failure_rate"] == 1
    assert "flaky-test diagnosis" in result["interpretation"]


def test_cohort_and_missing_time_rejected(make_run):
    a = make_run('<testsuite><testcase name="x"/></testsuite>', metadata={"platform": "linux"})
    b = make_run('<testsuite><testcase name="x"/></testsuite>', metadata={"platform": "windows"})
    with pytest.raises(InputError, match="cohort"):
        compare(a, b)
    with pytest.raises(InputError, match="cohort"):
        history([a, b])
    a["executed_at"] = None
    with pytest.raises(InputError, match="executed_at"):
        history([a])


def test_conflicting_run_id_rejected(make_run):
    a = make_run('<testsuite><testcase name="x"/></testsuite>')
    b = copy.deepcopy(a)
    b["scope"] = "other"
    with pytest.raises(InputError, match="Conflicting"):
        history([a, b])


def test_empty_current_run_reports_all_missing(make_run):
    first = make_run('<testsuite><testcase name="x"/></testsuite>')
    second = make_run('<testsuite tests="0"/>')
    assert compare(first, second)["changes"][0]["kind"] == "missing"


def test_tiny_duration_ratio_does_not_emit_infinity(make_run):
    first = make_run('<testsuite><testcase name="x" time="1e-300"/></testsuite>')
    second = make_run('<testsuite><testcase name="x" time="1e300"/></testsuite>')
    result = compare(first, second)
    assert result["changes"][0]["relative_change"] is None
    validate(result)
