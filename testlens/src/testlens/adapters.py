"""Conservative adapters for JUnit-style XML and CTest dashboard XML."""

from __future__ import annotations

import io
from dataclasses import dataclass
from pathlib import Path
from xml.etree.ElementTree import Element, ParseError

from defusedxml import ElementTree
from defusedxml.common import DefusedXmlException

from .io import Document, InputError, digest, finite_time, read_bytes


@dataclass(frozen=True)
class Limits:
    file_bytes: int = 64 * 1024 * 1024
    total_bytes: int = 256 * 1024 * 1024
    files: int = 1024
    depth: int = 64
    nodes: int = 1_000_000
    cases: int = 100_000
    output_chars: int = 64 * 1024


def tag(node: Element) -> str:
    return node.tag.rsplit("}", 1)[-1]


def children(node: Element, name: str) -> list[Element]:
    return [child for child in node if tag(child) == name]


def child_text(node: Element, name: str) -> str:
    matches = children(node, name)
    return "".join(matches[0].itertext()) if matches else ""


def parse_tree(data: bytes, limits: Limits) -> Element:
    try:
        depth = count = 0
        parser = ElementTree.iterparse(
            io.BytesIO(data),
            events=("start", "end"),
            forbid_dtd=True,
            forbid_entities=True,
            forbid_external=True,
        )
        for event, _node in parser:
            if event == "start":
                count += 1
                depth += 1
                if depth > limits.depth or count > limits.nodes:
                    raise InputError("XML depth/element budget exceeded")
            else:
                depth -= 1
        root: Element = parser.root
        return root
    except (ParseError, DefusedXmlException) as exc:
        raise InputError(f"Unsafe or malformed XML: {exc}") from exc


def _output(text: str, limits: Limits) -> Document:
    return {"text": text[: limits.output_chars], "truncated": len(text) > limits.output_chars}


def _properties(node: Element) -> dict[str, str]:
    result = {}
    for group in children(node, "properties"):
        for prop in children(group, "property"):
            key = prop.get("name", "")
            if key in result:
                raise InputError(f"Duplicate property: {key}")
            result[key] = prop.get("value", prop.text or "")
    return result


def _base_case(
    name: str,
    suite: str,
    classname: str,
    state: str,
    duration: float | None,
    source: str,
    locator: str,
    granularity: str,
    limits: Limits,
) -> Document:
    return {
        "name": name,
        "suite": suite,
        "classname": classname,
        "status": state,
        "duration_seconds": duration,
        "granularity": granularity,
        "source_id": source,
        "locator": locator,
        "source_file": None,
        "source_line": None,
        "output": _output("", limits),
        "messages": [],
        "properties": {},
        "attempt": None,
        "shard": None,
        "expected_outcome": None,
    }


def _diagnose_case(
    record: Document, raw_status: str, raw_duration: str | None, diagnostics: list[Document]
) -> None:
    """Apply the same observation-quality contract to every XML dialect."""
    if record["status"] == "unknown":
        diagnostics.append(
            {"code": "unsupported-status", "message": f"{record['locator']}: {raw_status!r}"}
        )
    if not record["name"]:
        record["status"] = "unknown"
        diagnostics.append({"code": "missing-name", "message": record["locator"]})
    if raw_duration is not None and record["duration_seconds"] is None:
        diagnostics.append({"code": "invalid-duration", "message": record["locator"]})


def junit(
    root: Element, source: str, dialect: str, limits: Limits
) -> tuple[list[Document], list[Document]]:
    cases: list[Document] = []
    diagnostics: list[Document] = []
    suites = [node for node in root.iter() if tag(node) == "testsuite"]
    if not suites:
        diagnostics.append({"code": "empty-report", "message": "No testsuite elements"})
    for suite_idx, suite in enumerate(suites, 1):
        name = suite.get("name", "")
        props = _properties(suite)
        entries = children(suite, "testcase")
        declared = suite.get("tests")
        if declared is not None:
            try:
                # Nested suite summary counts may include child suites.
                actual = len([n for n in suite.iter() if tag(n) == "testcase"])
                consistent = int(declared) == actual
            except ValueError:
                consistent = False
            if not consistent:
                diagnostics.append(
                    {
                        "code": "count-mismatch",
                        "message": f"Suite {name}: declared tests={declared}",
                    }
                )
        for attribute, element in [
            ("failures", "failure"),
            ("errors", "error"),
            ("skipped", "skipped"),
        ]:
            declared_outcomes = suite.get(attribute)
            if declared_outcomes is None:
                continue
            actual_outcomes = sum(
                bool(children(node, element)) for node in suite.iter() if tag(node) == "testcase"
            )
            try:
                consistent_outcomes = int(declared_outcomes) == actual_outcomes
            except ValueError:
                consistent_outcomes = False
            if not consistent_outcomes:
                diagnostics.append(
                    {
                        "code": "count-mismatch",
                        "message": (
                            f"Suite {name}: declared {attribute}={declared_outcomes}, "
                            f"observed {actual_outcomes}"
                        ),
                    }
                )
        for node in suite:
            if tag(node) in {"error", "failure"}:
                diagnostics.append(
                    {
                        "code": "suite-error",
                        "message": _output(
                            node.get("message", "") + " " + "".join(node.itertext()), limits
                        )["text"],
                        "suite": name,
                    }
                )
        for idx, case in enumerate(entries, 1):
            if len(cases) >= limits.cases:
                raise InputError("Testcase budget exceeded")
            errors = children(case, "error")
            failures = children(case, "failure")
            skipped = children(case, "skipped")
            state = (
                "error" if errors else "failed" if failures else "skipped" if skipped else "passed"
            )
            status = case.get("status", "").lower()
            if status in {"notrun", "not-run", "disabled"}:
                state = "not-run"
            elif status and status not in {
                "run",
                "passed",
                "failed",
                "notrun",
                "not-run",
                "disabled",
            }:
                state = "unknown"
            # CTest emits status=fail and status=notrun in some versions.
            if status in {"fail", "failed"}:
                state = "failed"
            duration = finite_time(case.get("time"))
            record = _base_case(
                case.get("name", ""),
                name,
                case.get("classname", ""),
                state,
                duration,
                source,
                f"testsuite[{suite_idx}]/testcase[{idx}]",
                "runner-target" if dialect == "ctest-junit" else "test-case",
                limits,
            )
            _diagnose_case(record, status, case.get("time"), diagnostics)
            record["properties"] = {**props, **_properties(case)}
            record["source_file"] = case.get("file")
            record["source_line"] = case.get("line")
            for item in errors + failures + skipped:
                record["messages"].append(
                    {
                        "kind": tag(item),
                        "type": item.get("type", ""),
                        "message": item.get("message", "")[: limits.output_chars],
                        **_output("".join(item.itertext()), limits),
                        "truncated": len(item.get("message", "")) > limits.output_chars
                        or len("".join(item.itertext())) > limits.output_chars,
                    }
                )
                if "xfail" in item.get("type", "").lower():
                    record["expected_outcome"] = "xfail"
                if "xpass" in item.get("type", "").lower() or "[XPASS" in item.get("message", ""):
                    record["expected_outcome"] = "xpass"
            outputs = [
                "".join(n.itertext()) for n in case if tag(n) in {"system-out", "system-err"}
            ]
            record["output"] = _output("\n".join(outputs), limits)
            # Retry extensions without a recognized attempt contract remain evidence, not retries.
            retry_nodes = [
                n
                for n in case
                if tag(n) in {"rerunFailure", "flakyFailure", "rerunError", "flakyError"}
            ]
            if retry_nodes:
                for node in retry_nodes:
                    record["messages"].append(
                        {
                            "kind": tag(node),
                            "type": node.get("type", ""),
                            "message": node.get("message", "")[: limits.output_chars],
                            **_output("".join(node.itertext()), limits),
                        }
                    )
                record["status"] = "unknown"
                diagnostics.append({"code": "unsupported-retry", "message": record["locator"]})
            attempt = record["properties"].get("testlens.attempt")
            if attempt is not None:
                try:
                    record["attempt"] = int(attempt)
                    if record["attempt"] < 1:
                        raise ValueError
                except ValueError:
                    record["attempt"] = None
                    diagnostics.append({"code": "invalid-attempt", "message": record["locator"]})
            record["shard"] = record["properties"].get("testlens.shard")
            cases.append(record)
    return cases, diagnostics


def ctest(root: Element, source: str, limits: Limits) -> tuple[list[Document], list[Document]]:
    records: list[Document] = []
    diagnostics: list[Document] = []
    testing_nodes = [n for n in root.iter() if tag(n) == "Testing"]
    for testing in testing_nodes:
        tests = children(testing, "Test")
        if not children(testing, "EndDateTime") and not children(testing, "EndTestTime"):
            diagnostics.append(
                {"code": "incomplete-dashboard", "message": "CTest completion timestamp absent"}
            )
        for idx, test in enumerate(tests, 1):
            if len(records) >= limits.cases:
                raise InputError("Testcase budget exceeded")
            measurements = {
                n.get("name", ""): child_text(n, "Value")
                for n in test.iter()
                if tag(n) == "NamedMeasurement"
            }
            raw_state = test.get("Status", "").lower()
            state = {"passed": "passed", "failed": "failed", "notrun": "not-run"}.get(
                raw_state, "unknown"
            )
            record = _base_case(
                child_text(test, "Name"),
                "CTest",
                "",
                state,
                finite_time(measurements.get("Execution Time")),
                source,
                f"Testing/Test[{idx}]",
                "runner-target",
                limits,
            )
            _diagnose_case(record, raw_state, measurements.get("Execution Time"), diagnostics)
            record["properties"] = measurements
            record["source_file"] = child_text(test, "Path") or None
            outputs = []
            for measurement in test.iter():
                if tag(measurement) != "Measurement":
                    continue
                for value in children(measurement, "Value"):
                    if value.get("encoding") or value.get("compression"):
                        diagnostics.append(
                            {
                                "code": "encoded-output",
                                "message": f"{record['name']}: encoded output not decoded",
                            }
                        )
                    else:
                        outputs.append("".join(value.itertext()))
            record["output"] = _output("\n".join(outputs), limits)
            records.append(record)
    if not records:
        diagnostics.append({"code": "empty-report", "message": "No CTest results"})
    return records, diagnostics


def read_report(
    path: Path, dialect: str, limits: Limits
) -> tuple[Document, list[Document], list[Document]]:
    data = read_bytes(path, limits.file_bytes)
    root = parse_tree(data, limits)
    actual = dialect
    if tag(root) in {"Site", "Testing"}:
        if dialect not in {"auto", "ctest"}:
            raise InputError(f"Expected {dialect}, found CTest dashboard")
        actual = "ctest"
    elif tag(root) in {"testsuites", "testsuite"}:
        if dialect == "ctest":
            raise InputError("Expected CTest dashboard, found JUnit")
        if dialect == "auto":
            # Do not guess pytest/Qt/CTest from user-controlled suite names.
            actual = "junit"
    else:
        raise InputError(f"Unsupported XML root: {tag(root)}")
    identity = digest(data)
    source = {
        "id": identity,
        "path": str(path),
        "sha256": identity,
        "bytes": len(data),
        "dialect": actual,
    }
    records, diagnostics = (
        ctest(root, identity, limits)
        if actual == "ctest"
        else junit(root, identity, actual, limits)
    )
    for diagnostic in diagnostics:
        diagnostic["source_id"] = identity
    return source, records, diagnostics
