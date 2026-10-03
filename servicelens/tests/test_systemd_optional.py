"""Optional differential sanity check; verify never activates a service."""

import os
import shutil
import subprocess

import pytest

from servicelens import check, inspect


@pytest.mark.skipif(
    os.environ.get("SERVICELENS_SYSTEMD_VERIFY") != "1" or shutil.which("systemd-analyze") is None,
    reason="opt-in systemd-analyze verify comparison",
)
def test_verify_agrees_for_benign_fixture(image):
    root, write = image
    true_binary = shutil.which("true")
    assert true_binary
    path = write(
        "/usr/lib/systemd/system/servicelens-fixture.service",
        "[Unit]\nDescription=ServiceLens verification fixture\n"
        f"[Service]\nType=oneshot\nExecStart={true_binary}\nEnvironment=VALUE=example\n",
    )
    result = subprocess.run(
        ["systemd-analyze", "verify", "--man=no", "--generators=no", str(path)],
        capture_output=True,
        text=True,
        timeout=15,
    )
    assert result.returncode == 0, result.stderr
    assert check(inspect("servicelens-fixture.service", root=root))["errors"] == 0
