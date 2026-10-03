import copy
import json
import os
import subprocess
import sys
from pathlib import Path

import jsonschema
import pytest

from servicelens import InputError, diff, inspect, load, save, validate

EXAMPLE = Path(__file__).parents[1] / "examples/rootfs"


def example():
    return inspect("worker@blue.service", root=EXAMPLE)


def test_schema_and_roundtrip(tmp_path):
    snapshot = example()
    schema = json.loads(
        (Path(__file__).parents[1] / "schemas/servicelens-snapshot-v1.schema.json").read_text()
    )
    jsonschema.Draft202012Validator(schema).validate(snapshot)
    validate(snapshot)
    path = tmp_path / "snapshot.json"
    save(snapshot, path)
    assert load(path) == snapshot
    assert path.stat().st_mode & 0o777 == 0o600
    delta = diff(snapshot, snapshot)
    validate(delta)
    diff_schema = json.loads(
        (Path(__file__).parents[1] / "schemas/servicelens-diff-v1.schema.json").read_text()
    )
    jsonschema.Draft202012Validator(diff_schema).validate(delta)


@pytest.mark.parametrize(
    "mutation",
    [
        lambda d: d.update(schema="new/version"),
        lambda d: d.update(unknown=True),
        lambda d: d.update(partial="false"),
        lambda d: d["units"]["worker@blue.service"]["ledger"][0].update(line=True),
        lambda d: d["units"]["worker@blue.service"].update(commands=[]),
        lambda d: d["diagnostics"].append(
            {"code": "x", "message": "x", "path": None, "line": None, "severity": "error"}
        ),
    ],
)
def test_strict_consumer_rejects_malformed_shapes(mutation):
    snapshot = copy.deepcopy(example())
    mutation(snapshot)
    with pytest.raises(InputError):
        validate(snapshot)


def test_duplicate_json_and_special_file_rejected(tmp_path):
    path = tmp_path / "bad.json"
    path.write_text('{"schema":"servicelens.snapshot/v1","schema":"other"}')
    with pytest.raises(InputError):
        load(path)
    pipe = tmp_path / "fifo"
    os.mkfifo(pipe)
    with pytest.raises(InputError):
        load(pipe)


def test_save_cannot_clobber_input_or_links(tmp_path):
    source = tmp_path / "source"
    source.write_text("precious")
    target = tmp_path / "target"
    target.hardlink_to(source)
    for output in (source, target):
        with pytest.raises(InputError):
            save(example(), output, inputs=[source])
    target.unlink()
    target.symlink_to(source)
    with pytest.raises(InputError):
        save(example(), target)
    assert source.read_text() == "precious"


def run(*args):
    return subprocess.run(
        [sys.executable, "-m", "servicelens", *map(str, args)],
        capture_output=True,
        text=True,
        timeout=10,
    )


def test_end_to_end_cli(tmp_path):
    common = ["worker@blue.service", "--root", EXAMPLE]
    assert run("inspect", *common).returncode == 0
    result = run("explain", *common, "--key", "Service.ExecStart")
    assert result.returncode == 0 and "[reset]" in result.stdout
    assert "digraph services" in run("graph", *common).stdout
    before, after = tmp_path / "before.json", tmp_path / "after.json"
    assert run("snapshot", *common, "--output", before).returncode == 0
    assert run("snapshot", *common, "--output", after).returncode == 0
    assert run("check", after, "--fail-on", "error,unknown").returncode == 0
    assert run("diff", before, after).returncode == 0
    assert run("diff", before, after, "--fail-on", "unknown").returncode == 1
    assert run("check", after, "--fail-on", "typo").returncode == 2
    assert run("inspect", *common, "--format", "typo").returncode == 2
    assert run("inspect", *common, "--max-bytes", "0").returncode == 2


def test_cli_refuses_input_output_alias(image):
    root, write = image
    source = write("/usr/lib/systemd/system/app.service", "[Service]\nExecStart=/bin/true\n")
    assert run("snapshot", "app.service", "--root", root, "--output", source).returncode == 2
    assert source.read_text().startswith("[Service]")
