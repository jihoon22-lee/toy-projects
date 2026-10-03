"""Measure bounded collection; writes fixtures only to product scratch space."""

import argparse
import json
import resource
import tempfile
import time
from pathlib import Path

from testlens.adapters import Limits
from testlens.core import collect
from testlens.io import InputError

parser = argparse.ArgumentParser()
parser.add_argument("--cases", type=int, default=10000)
args = parser.parse_args()
if not 1 <= args.cases <= 100000:
    parser.error("--cases must be 1..100000")
root = Path(__file__).resolve().parents[1] / ".artifacts"
root.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix="benchmark-", dir=root) as scratch:
    path = Path(scratch) / "large.xml"
    with path.open("w") as handle:
        handle.write(f'<testsuite name="benchmark" tests="{args.cases}">')
        for index in range(args.cases):
            handle.write(f'<testcase name="test[{index}]" time="0.001"/>')
        handle.write("</testsuite>")
    start = time.monotonic()
    run = collect([path], project="bench", run_id="bench", declared_complete=True)
    elapsed = time.monotonic() - start
    assert len(run["tests"]) == args.cases
    try:
        collect([path], project="bench", run_id="limit", limits=Limits(cases=args.cases - 1))
    except InputError:
        bounded = True
    else:
        raise RuntimeError("Case limit was not enforced")
    print(
        json.dumps(
            {
                "cases": args.cases,
                "seconds": elapsed,
                "max_rss_platform_units": resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
                "limit_enforced": bounded,
            }
        )
    )
