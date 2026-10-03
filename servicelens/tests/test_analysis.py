import copy
import json

import pytest

from servicelens import Limits, check, diff, graph, inspect, validate
from servicelens.analysis import specifiers
from servicelens.report import text

SERVICE = "/usr/lib/systemd/system/app.service"
BODY = "[Service]\nType=simple\nExecStart=/bin/true\n"


def capture(image, body=BODY, **kwargs):
    root, write = image
    write(SERVICE, body)
    return inspect("app.service", root=root, redact=False, **kwargs)


def test_complete_capture_and_independent_schema(image):
    document = capture(image)
    validate(document)
    assert not document["partial"]
    assert check(document)["errors"] == 0
    assert document["units"]["app.service"]["commands"]["ExecStart"][0]["executable"] == "/bin/true"


def test_precedence_lexical_order_and_source_ledger(image):
    root, write = image
    write(SERVICE, BODY + "Restart=no\n")
    write("/etc/systemd/system/app.service.d/20-restart.conf", "[Service]\nRestart=always\n")
    write("/run/systemd/system/app.service.d/10-restart.conf", "[Service]\nRestart=on-failure\n")
    write(
        "/usr/lib/systemd/system/app.service.d/20-restart.conf", "[Service]\nRestart=on-success\n"
    )
    document = inspect("app.service", root=root, redact=False)
    unit = document["units"]["app.service"]
    assert unit["settings"]["Service.Restart"]["value"] == "always"
    assert sum(not d["selected"] for d in unit["resolution"]["dropins"]) == 1
    ledger = [x for x in unit["ledger"] if x["key"] == "Restart"]
    assert [x["value"] for x in ledger] == ["no", "on-failure", "always"]
    assert ledger[-1]["line"] == 2
    assert "20-restart.conf:2" in text(document, key="Service.Restart")


def test_main_unit_replacement_keeps_vendor_dropins(image):
    root, write = image
    write(SERVICE, BODY)
    write("/etc/systemd/system/app.service", "[Service]\nExecStart=/bin/echo local\n")
    write("/usr/lib/systemd/system/app.service.d/90-policy.conf", "[Service]\nRestart=always\n")
    unit = inspect("app.service", root=root, redact=False)["units"]["app.service"]
    assert unit["resolution"]["selected"] == "/etc/systemd/system/app.service"
    assert unit["commands"]["ExecStart"][0]["executable"] == "/bin/echo"
    assert unit["settings"]["Service.Restart"]["value"] == "always"


def test_template_instance_prefix_and_type_dropins(image):
    root, write = image
    write("/usr/lib/systemd/system/api-worker@.service", BODY)
    for scope, value in (
        ("service", "no"),
        ("api-.service", "on-failure"),
        ("api-worker@.service", "on-success"),
        ("api-worker@blue.service", "always"),
    ):
        write(f"/etc/systemd/system/{scope}.d/10-restart.conf", f"[Service]\nRestart={value}\n")
    document = inspect("api-worker@blue.service", root=root, redact=False)
    unit = document["units"]["api-worker@blue.service"]
    assert unit["resolution"]["canonical"] == "api-worker@blue.service"
    assert unit["settings"]["Service.Restart"]["value"] == "always"


def test_alias_contributes_dropins(image):
    root, write = image
    write(SERVICE, BODY)
    write("/etc/systemd/system/alias.service", link=SERVICE)
    write("/etc/systemd/system/alias.service.d/10-restart.conf", "[Service]\nRestart=always\n")
    unit = inspect("app.service", root=root, redact=False)["units"]["app.service"]
    assert "alias.service" in unit["resolution"]["names"]
    assert unit["settings"]["Service.Restart"]["value"] == "always"
    alias = inspect("alias.service", root=root, redact=False)["units"]["alias.service"]
    assert alias["resolution"]["canonical"] == "app.service"


def test_unit_and_dropin_mask(image):
    root, write = image
    write(SERVICE, BODY)
    write("/etc/systemd/system/app.service", link="/dev/null")
    unit = inspect("app.service", root=root)["units"]["app.service"]
    assert unit["resolution"]["masked"]
    assert not unit["commands"]
    (root / "etc/systemd/system/app.service").unlink()
    write("/usr/lib/systemd/system/app.service.d/10-restart.conf", "[Service]\nRestart=always\n")
    write("/etc/systemd/system/app.service.d/10-restart.conf", link="/dev/null")
    unit = inspect("app.service", root=root)["units"]["app.service"]
    assert "Service.Restart" not in unit["settings"]


def test_alias_loop_produces_unknown_not_vendor_success(image):
    root, write = image
    write(SERVICE, BODY)
    write("/etc/systemd/system/app.service", link="other.service")
    write("/etc/systemd/system/other.service", link="app.service")
    document = inspect("app.service", root=root)
    assert document["partial"]
    assert check(document)["unknown"]
    assert not document["units"]["app.service"]["commands"]


def test_command_reset_and_multiple_start_validation(image):
    root, write = image
    write(SERVICE, BODY)
    path = "/etc/systemd/system/app.service.d/override.conf"
    write(path, "[Service]\nExecStart=/bin/echo hi\n")
    assert check(inspect("app.service", root=root))["errors"] == 1
    write(path, "[Service]\nExecStart=\nExecStart=/bin/echo hi\n")
    document = inspect("app.service", root=root, redact=False)
    unit = document["units"]["app.service"]
    assert not check(document)["errors"]
    assert len(unit["commands"]["ExecStart"]) == 1
    assert any(entry["action"] == "reset" for entry in unit["ledger"])


def test_dependency_empty_does_not_reset_and_ordering_is_distinct(image):
    root, write = image
    write(SERVICE, "[Unit]\nWants=db.service\nAfter=db.service\nWants=\n" + BODY)
    write("/usr/lib/systemd/system/db.service", BODY)
    document = inspect("app.service", root=root)
    unit = document["units"]["app.service"]
    assert {e["relation"] for e in unit["dependencies"]} == {"Wants", "After"}
    assert unit["settings"]["Unit.Wants"]["value"] == ["db.service"]
    assert "db.service" in document["units"]
    assert "style=dashed" in graph(document)
    assert check(document)["warnings"] == 1


def test_dependency_links_are_collected(image):
    root, write = image
    write(SERVICE, BODY)
    write("/usr/lib/systemd/system/db.service", BODY)
    write(
        "/etc/systemd/system/app.service.requires/db.service",
        link="/usr/lib/systemd/system/db.service",
    )
    snapshot = inspect("app.service", root=root)
    assert snapshot["units"]["app.service"]["dependencies"][0]["relation"] == "Requires"


def test_environment_precedence_unset_and_pre_unset_command(image):
    root, write = image
    write(
        SERVICE,
        "[Service]\nEnvironment=TOKEN=first KEEP=unit\n"
        "EnvironmentFile=/etc/a.env\nEnvironmentFile=/etc/b.env\n"
        "UnsetEnvironment=TOKEN\nExecStart=/bin/echo ${TOKEN}\n",
    )
    write("/etc/a.env", "TOKEN=second\nKEEP=file\n")
    write("/etc/b.env", "TOKEN=last\n")
    document = inspect("app.service", root=root, redact=False)
    unit = document["units"]["app.service"]
    assert "TOKEN" not in unit["environment"]
    assert unit["environment"]["KEEP"]["value"] == "file"
    assert unit["commands"]["ExecStart"][0]["arguments"] == ["last"]
    assert unit["environment_history"][-1]["origin"]["path"] == "/etc/b.env"


def test_environment_list_reset_optional_missing_and_wildcards(image):
    root, write = image
    write(
        SERVICE,
        BODY + "Environment=OLD=yes\nEnvironment=\nEnvironment=NEW=yes\n"
        "EnvironmentFile=/missing\nEnvironmentFile=\nEnvironmentFile=-/optional\n"
        "EnvironmentFile=/etc/config/*.env\n",
    )
    write("/etc/config/10.env", "VALUE=one\n")
    write("/etc/config/20.env", "VALUE=two\n")
    document = inspect("app.service", root=root, redact=False)
    env = document["units"]["app.service"]["environment"]
    assert "OLD" not in env and env["NEW"]["value"] == "yes"
    assert env["VALUE"]["value"] == "two"
    assert not document["partial"]


def test_required_environment_missing_unknown_specifier_and_directive(image):
    document = capture(
        image, BODY + "EnvironmentFile=/missing\nMystery=secret\nWorkingDirectory=%h\n"
    )
    assert document["partial"]
    assert check(document)["errors"] == 1
    assert check(document)["unknown"] == 2


def test_supported_specifiers():
    assert specifiers("%n %N %p %i %I %%", "worker@blue-red.service") == (
        "worker@blue-red.service worker@blue-red worker blue-red blue/red %",
        [],
    )
    assert specifiers("%h", "app.service")[1] == ["%h"]


def test_redaction_including_superseded_values_and_diff(image):
    root, write = image
    write(
        SERVICE,
        BODY + "Environment=TOKEN=supersecret\nEnvironment=TOKEN=updatedsecret\n"
        "Mystery=hiddensecret\n",
    )
    masked = inspect("app.service", root=root)
    raw = inspect("app.service", root=root, redact=False)
    serialized = json.dumps(masked)
    assert all(
        secret not in serialized for secret in ("supersecret", "updatedsecret", "hiddensecret")
    )
    result = diff(masked, raw)
    assert result["unknown"]
    assert all(
        secret not in json.dumps(result)
        for secret in ("supersecret", "updatedsecret", "hiddensecret")
    )
    assert diff(masked, masked)["unknown"]
    assert not diff(masked, masked)["changed"]


def test_diff_value_source_order_and_evidence_changes(image):
    before = capture(image, BODY + "EnvironmentFile=-/a\nEnvironmentFile=-/b\n")
    after = copy.deepcopy(before)
    after["units"]["app.service"]["settings"]["Service.EnvironmentFile"]["value"].reverse()
    assert any(c["kind"] == "value-changed" for c in diff(before, after)["changes"])
    after = copy.deepcopy(before)
    after["units"]["app.service"]["settings"]["Service.Type"]["origins"][0]["line"] = 50
    assert any(c["kind"] == "origin-changed" for c in diff(before, after)["changes"])


def test_graph_limits_produce_partial(image):
    root, write = image
    for i in range(4):
        write(f"/usr/lib/systemd/system/a{i}.service", f"[Unit]\nWants=a{i + 1}.service\n" + BODY)
    document = inspect("a0.service", root=root, limits=Limits(depth=1))
    assert document["partial"] and len(document["units"]) == 2
    document = inspect("a0.service", root=root, limits=Limits(nodes=1))
    assert document["partial"] and len(document["units"]) == 1


@pytest.mark.parametrize(
    "body", ["garbage", '[Service]\nExecStart="unterminated', "[Service]\n1Bad=x\n"]
)
def test_invalid_syntax_never_succeeds(image, body):
    assert check(capture(image, body))["errors"]


def test_specifier_expansion_cannot_split_arguments(image):
    root, write = image
    write(
        "/usr/lib/systemd/system/worker@.service",
        "[Service]\nExecStart=/bin/echo %I\nEnvironment=QUEUE=%I\n",
    )
    name = r"worker@blue\x20red.service"
    unit = inspect(name, root=root, redact=False)["units"][name]
    assert unit["commands"]["ExecStart"][0]["arguments"] == ["blue red"]
    assert unit["environment"]["QUEUE"]["value"] == "blue red"


def test_alias_to_masked_unit(image):
    root, write = image
    write("/etc/systemd/system/app.service", link="masked.service")
    write("/etc/systemd/system/masked.service", link="/dev/null")
    unit = inspect("app.service", root=root)["units"]["app.service"]
    assert unit["resolution"]["masked"]


def test_unreadable_environment_invalidates_candidate_values(image):
    document = capture(
        image,
        "[Service]\nEnvironment=TOKEN=candidate\nEnvironmentFile=/missing\n"
        "ExecStart=/bin/echo ${TOKEN}\n",
    )
    unit = document["units"]["app.service"]
    assert unit["environment"]["TOKEN"]["status"] == "unknown"
    assert unit["commands"]["ExecStart"][0]["status"] == "unknown"


def test_invalid_dependency_then_valid_assignment_does_not_crash(image):
    document = capture(image, '[Unit]\nWants="unfinished\nWants=db.service\n' + BODY)
    assert document["partial"]


def test_ordering_cycle_but_not_requirement_cycle_is_an_error(image):
    root, write = image
    write(SERVICE, "[Unit]\nWants=db.service\n" + BODY)
    write("/usr/lib/systemd/system/db.service", "[Unit]\nWants=app.service\n" + BODY)
    assert not check(inspect("app.service", root=root))["errors"]
    write(SERVICE, "[Unit]\nAfter=db.service\n" + BODY)
    write("/usr/lib/systemd/system/db.service", "[Unit]\nAfter=app.service\n" + BODY)
    assert any(
        d["code"] == "ordering-cycle" for d in inspect("app.service", root=root)["diagnostics"]
    )


def test_invalid_enum_is_not_known_success(image):
    document = capture(image, "[Service]\nType=imaginary\nExecStart=/bin/true\n")
    assert check(document)["errors"]
    assert document["units"]["app.service"]["settings"]["Service.Type"]["status"] == "unknown"


def test_global_directive_budget(image):
    root, write = image
    write(SERVICE, "[Unit]\nWants=db.service\n" + BODY)
    write("/usr/lib/systemd/system/db.service", BODY)
    document = inspect("app.service", root=root, limits=Limits(directives=4))
    assert sum(len(u["ledger"]) for u in document["units"].values()) <= 4
    assert document["partial"]


def test_dependency_link_outside_unit_paths_is_not_known(image):
    root, write = image
    write(SERVICE, BODY)
    write("/etc/systemd/system/app.service.wants/db.service", link="/etc/private.service")
    document = inspect("app.service", root=root)
    assert document["partial"]
    assert not document["units"]["app.service"]["dependencies"]


def test_empty_unit_is_a_mask(image):
    root, write = image
    write(SERVICE, "")
    write("/etc/systemd/system/app.service.d/10.conf", BODY)
    document = inspect("app.service", root=root)
    assert document["units"]["app.service"]["resolution"]["masked"]
    assert not document["units"]["app.service"]["commands"]
    assert not check(document)["errors"]


def test_terminal_controls_are_escaped(image):
    root, write = image
    write(SERVICE, '[Service]\nExecStart=/bin/echo "\\x1b[31m"\n')
    assert "\x1b" not in text(inspect("app.service", root=root, redact=False))


def test_utf8_escaped_instance_specifier():
    assert specifiers("%I", r"worker@\xc3\xa9.service") == ("é", [])
    assert specifiers("%I", r"worker@\xff.service")[1] == ["%I"]


def test_opt_in_supported_service_defaults(image):
    document = capture(image, include_defaults=True)
    unit = document["units"]["app.service"]
    edges = {(edge["relation"], edge["target"]) for edge in unit["dependencies"]}
    assert ("Requires", "sysinit.target") in edges
    assert ("After", "basic.target") in edges
    assert ("Before", "shutdown.target") in edges
    assert all(edge["origin"]["path"].startswith("systemd-255:") for edge in unit["dependencies"])
    validate(document)


def test_default_dependencies_no_preserves_dbus_implicit_edges(image):
    document = capture(
        image,
        "[Unit]\nDefaultDependencies=no\n[Service]\nType=dbus\nBusName=com.example.App\nExecStart=/bin/true\n",
        include_defaults=True,
    )
    edges = document["units"]["app.service"]["dependencies"]
    assert {(edge["relation"], edge["target"]) for edge in edges} == {
        ("Requires", "dbus.socket"),
        ("After", "dbus.socket"),
    }
    validate(document)


def test_invalid_default_setting_stays_unknown(image):
    document = capture(image, "[Unit]\nDefaultDependencies=perhaps\n" + BODY, include_defaults=True)
    unit = document["units"]["app.service"]
    assert not unit["dependencies"]
    assert any(item["code"] == "default-dependencies-unknown" for item in unit["diagnostics"])
    assert document["partial"]
