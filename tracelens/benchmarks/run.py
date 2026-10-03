#!/usr/bin/env python3
"""Generate a trace, measure wall time/RSS, and verify observed call counts.
Linux /usr/bin/time supplies per-process RSS; no third-party Python packages.
"""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import time

p = argparse.ArgumentParser()
p.add_argument('binary', type=Path)
p.add_argument('--mib', type=int, default=16)
p.add_argument('--output', type=Path)
a = p.parse_args()
if not 1 <= a.mib <= 4096:
    p.error('--mib must be between 1 and 4096')
with tempfile.TemporaryDirectory(prefix='tracelens-benchmark-') as tmp:
    tmp = Path(tmp)
    trace, snapshot, metrics = tmp/'generated.strace', tmp/'snapshot.json', tmp/'time.txt'
    line = b'100 1700000000.000001 openat(AT_FDCWD, "/tmp/item", O_RDONLY) = -1 ENOENT (No such file) <0.000020>\n'
    count = a.mib * 1024 * 1024 // len(line)
    with trace.open('wb') as f:
        for start in range(0, count, 10000):
            f.write(line * min(10000, count-start))
    start = time.monotonic()
    subprocess.run(['/usr/bin/time', '-f', '%e %M', '-o', str(metrics), str(a.binary.resolve()), 'inspect', str(trace), '--format', 'json', '--output', str(snapshot), '--max-bytes', str(trace.stat().st_size+1), '--max-events', str(count+1), '--top', '10'], check=True)
    elapsed = time.monotonic()-start
    observed = json.loads(snapshot.read_bytes())
    assert int(observed['calls']) == count and not observed['partial']
    seconds, rss = metrics.read_text().split()
    # Cancellation is measured on a separate child, leaving the completed-run metrics intact.
    child = subprocess.Popen([str(a.binary.resolve()), 'inspect', str(trace), '--max-events', str(count+1), '--max-bytes', str(trace.stat().st_size+1)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    time.sleep(0.02)
    cancel_start = time.monotonic()
    child.terminate()
    child.communicate(timeout=10)
    cancellation_ms = round((time.monotonic()-cancel_start)*1000, 3)
    result = {'schema':'tracelens.benchmark/v1', 'input_bytes':trace.stat().st_size, 'calls':count, 'wall_seconds':round(elapsed, 3), 'process_seconds':float(seconds), 'max_rss_kib':int(rss), 'cancel_exit':child.returncode, 'cancel_latency_ms':cancellation_ms, 'note':'Local observations, not performance guarantees; cancellation may follow completed input on very small runs.'}
    encoded = json.dumps(result, indent=2)+'\n'
    if a.output: a.output.write_text(encoded)
    print(encoded, end='')
