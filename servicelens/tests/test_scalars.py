"""Behavioral contracts derived from systemd v255's directive parser families."""

import json
import subprocess
import sys

import pytest

from servicelens import check, inspect, load, save, validate
from servicelens.scalars import InputError, parse_timespan

UNIT_PATH = "/usr/lib/systemd/system/app.service"


def capture(image, settings, *, section="Service", unit="app.service", **options):
    root, write = image
    write(
        UNIT_PATH.replace("app.service", unit),
        "[Service]\nExecStart=/bin/true\n" + f"[{section}]\n{settings}\n",
    )
    return inspect(unit, root=root, redact=False, **options)


@pytest.mark.parametrize(
    "assignment",
    [
        "NoNewPrivileges=definitely-not-a-boolean",
        "RestartSec=not-a-duration",
        "UMask=invalid",
        "RemainAfterExit=maybe",
        "PrivateTmp=disconnected",
        "DynamicUser=2",
        "ProtectHome=full",
        "ProtectSystem=read-only",
        "KillMode=kill-everything",
        "StandardOutput=not-an-output",
        "StandardError=append:relative",
        "WorkingDirectory=relative",
        "RootDirectory=../root",
        "BusName=singlecomponent",
        "User=65535",
        "Group=4294967295",
        "User=999999999999999999999",
        "TimeoutStartSec=-0",
        "TimeoutStopSec=1fortnight",
        "RestartSec=1.2.3",
        "UMask=0088",
        "UMask=10000",
        "UMask=+022",
        "UMask=0o022",
        "NoNewPrivileges=%i",
    ],
)
def test_invalid_scalar_is_unknown_and_fails_check(image, assignment):
    document = capture(image, assignment)
    key = "Service." + assignment.split("=", 1)[0]
    assert document["units"]["app.service"]["settings"][key]["status"] == "unknown"
    assert document["partial"]
    assert check(document)["errors"] >= 1
    evidence = document["units"]["app.service"]["ledger"][-1]
    assert evidence["status"] == "unknown" and evidence["action"] == "invalid"
    assert any(
        d["line"] == 4 and d["code"] in {"invalid-scalar", "invalid-enum"}
        for d in check(document)["diagnostics"]
    )
    validate(document)


@pytest.mark.parametrize(
    "value,expected",
    [
        ("yes", "yes"),
        ("TRUE", "yes"),
        ("Y", "yes"),
        ("t", "yes"),
        ("On", "yes"),
        ("1", "yes"),
        ("no", "no"),
        ("FALSE", "no"),
        ("N", "no"),
        ("f", "no"),
        ("Off", "no"),
        ("0", "no"),
    ],
)
def test_boolean_aliases_and_default_dependency_rules(image, value, expected):
    document = capture(image, f"DefaultDependencies={value}", section="Unit", include_defaults=True)
    unit = document["units"]["app.service"]
    assert unit["settings"]["Unit.DefaultDependencies"]["value"] == expected
    defaults = [
        edge for edge in unit["dependencies"] if edge["origin"]["path"].startswith("systemd-255:")
    ]
    assert bool(defaults) == (expected == "yes")
    assert not any(d["code"] == "invalid-scalar" for d in check(document)["diagnostics"])


@pytest.mark.parametrize(
    "value,usec",
    [
        ("0", 0),
        ("0.5", 500000),
        (".5", 500000),
        ("+2", 2000000),
        ("1min 500ms", 60500000),
        ("1h30min", 5400000000),
        ("1second 2seconds", 3000000),
        ("1msec 2usec", 1002),
        ("1μs 2µs", 3),
        ("1y 1M", 34187400000000),
        ("1week", 604800000000),
        ("1 2", 3000000),
        ("1.5 .5", 2000000),
        ("1s.5", 1500000),
        ("0.0000001s", 0),
        ("infinity", 2**64 - 1),
        ("9223372036854775807us", 2**63 - 1),
    ],
)
def test_timespan_v255_grammar_and_resolution(value, usec):
    assert parse_timespan(value) == usec


@pytest.mark.parametrize(
    "value",
    [
        "",
        " ",
        "-0",
        "-1s",
        "Infinity",
        "infinity 1",
        "1e3",
        "0x10",
        "1ns",
        "1. s",
        "1.2.3",
        "+.5",
        "1s junk",
        "18446744073709551615us",
        "18446744073710s",
        "9223372036854775807us 9223372036854775807us 1us",
        "9" * 5000,
    ],
)
def test_invalid_or_overflowing_duration_does_not_become_a_value(value):
    with pytest.raises(InputError):
        parse_timespan(value)


@pytest.mark.parametrize(
    "assignment,expected",
    [
        ("UMask=022", "0022"),
        ("UMask=00000007777", "7777"),
        ("UMask=0", "0000"),
        ("ProtectHome=read-only", "read-only"),
        ("ProtectHome=tmpfs", "tmpfs"),
        ("ProtectSystem=strict", "strict"),
        ("ProtectSystem=FULL", None),
        ("ProtectSystem=Y", "yes"),
        ("WorkingDirectory=-~", "-~"),
        ("WorkingDirectory=-/var//lib/app/.", "-/var/lib/app"),
        ("RootDirectory=/", "/"),
        ("PIDFile=daemon.pid", "/run/daemon.pid"),
        ("PIDFile=/var/run/app.pid", "/run/app.pid"),
        ("BusName=org.example.App", "org.example.App"),
        ("BusName=:1.42", ":1.42"),
        ("User=domain user@example", "domain user@example"),
        ("Group=4294967294", "4294967294"),
        ("StandardOutput=fd:descriptor name", "fd:descriptor name"),
        ("StandardOutput=fd:", "fd:"),
        ("StandardError=append:/var/log/app", "append:/var/log/app"),
        ("KillMode=mixed", "mixed"),
        ("TimeoutStartSec=0", "infinity"),
        ("TimeoutStopSec=0", "infinity"),
        ("RestartSec=0", "0"),
    ],
)
def test_directive_specific_valid_values(image, assignment, expected):
    document = capture(image, assignment)
    setting = document["units"]["app.service"]["settings"]["Service." + assignment.split("=", 1)[0]]
    if expected is None:
        assert setting["status"] == "unknown"
    else:
        assert setting["status"] == "known" and setting["value"] == expected
        assert check(document)["errors"] == 0


@pytest.mark.parametrize(
    "key,first",
    [
        ("NoNewPrivileges", "yes"),
        ("RestartSec", "5min"),
        ("UMask", "0077"),
        ("Type", "oneshot"),
        ("ProtectHome", "read-only"),
    ],
)
def test_empty_non_reset_scalar_keeps_previous_candidate_and_origin(image, key, first):
    document = capture(image, f"{key}={first}\n{key}=")
    setting = document["units"]["app.service"]["settings"]["Service." + key]
    assert setting["value"] == first
    assert setting["status"] == "unknown"
    assert setting["origins"] == [{"path": UNIT_PATH, "line": 4}]
    assert check(document)["errors"] == 1


@pytest.mark.parametrize(
    "key,first",
    [
        ("User", "daemon"),
        ("Group", "daemon"),
        ("RootDirectory", "/old"),
        ("WorkingDirectory", "/old"),
        ("PIDFile", "old.pid"),
    ],
)
def test_supported_empty_assignment_resets(image, key, first):
    document = capture(image, f"{key}={first}\n{key}=")
    unit = document["units"]["app.service"]
    setting = unit["settings"]["Service." + key]
    assert setting["value"] == "" and setting["status"] == "known"
    assert unit["ledger"][-1]["action"] == "reset"
    assert check(document)["errors"] == 0


def test_invalid_dropin_does_not_replace_prior_valid_candidate(image):
    root, write = image
    write(
        UNIT_PATH,
        "[Service]\nExecStart=/bin/true\nNoNewPrivileges=yes\nRestartSec=2min\nUMask=0077\n",
    )
    override = "/etc/systemd/system/app.service.d/20-bad.conf"
    write(override, "[Service]\nNoNewPrivileges=bad\nRestartSec=bad\nUMask=bad\n")
    document = inspect("app.service", root=root, redact=False)
    settings = document["units"]["app.service"]["settings"]
    assert [
        settings["Service." + key]["value"] for key in ("NoNewPrivileges", "RestartSec", "UMask")
    ] == ["yes", "2min", "0077"]
    assert all(
        settings["Service." + key]["origins"][0]["path"] == UNIT_PATH
        for key in ("NoNewPrivileges", "RestartSec", "UMask")
    )
    assert check(document)["errors"] == 3
    assert all(d["path"] == override for d in check(document)["diagnostics"])


def test_scalar_specifiers_only_where_systemd_expands_them(image):
    document = capture(image, "NoNewPrivileges=%i\nUser=%i\nUMask=%i", unit="worker@yes.service")
    settings = document["units"]["worker@yes.service"]["settings"]
    assert settings["Service.User"]["value"] == "yes"
    assert settings["Service.NoNewPrivileges"]["status"] == "unknown"
    assert settings["Service.UMask"]["status"] == "unknown"


def test_delegate_lists_accumulate_and_explicit_reset_clears(image):
    document = capture(image, "Delegate=cpu\nDelegate=memory")
    unit = document["units"]["app.service"]
    assert unit["settings"]["Service.Delegate"]["value"] == "cpu memory"
    assert len(unit["settings"]["Service.Delegate"]["origins"]) == 2
    document = capture(image, "Delegate=yes\nDelegate=\nDelegate=io")
    assert document["units"]["app.service"]["settings"]["Service.Delegate"]["value"] == "io"
    assert check(document)["errors"] == 0
    document = capture(image, "Delegate=cpu unknown-controller")
    assert document["units"]["app.service"]["settings"]["Service.Delegate"]["value"] == "cpu"
    assert check(document)["errors"] == 1


def test_bus_name_invalid_does_not_infer_dbus_type(image):
    document = capture(image, "BusName=bad", include_defaults=True)
    assert all(
        edge["target"] != "dbus.socket" for edge in document["units"]["app.service"]["dependencies"]
    )


def test_root_directory_reset_restores_image_executable_observation(image):
    _, write = image
    write("/bin/true", "fixture")
    document = capture(image, "RootDirectory=/old\nRootDirectory=")
    assert document["units"]["app.service"]["commands"]["ExecStart"][0]["existence"] == "present"


def test_reviewed_failure_survives_redaction_snapshot_and_cli_check(image, tmp_path):
    root, _ = image
    document = capture(
        image, "NoNewPrivileges=definitely-not-a-boolean\nRestartSec=not-a-duration\nUMask=invalid"
    )
    assert check(document)["errors"] == 3
    masked = inspect("app.service", root=root)
    output = tmp_path / "snapshot.json"
    save(masked, output)
    assert check(load(output))["errors"] == 3
    process = subprocess.run(
        [sys.executable, "-m", "servicelens", "check", str(output), "--format", "json"],
        capture_output=True,
        text=True,
        timeout=10,
    )
    assert process.returncode == 1
    assert json.loads(process.stdout)["errors"] == 3
