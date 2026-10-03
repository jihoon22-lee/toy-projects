#!/usr/bin/env python3
"""Black-box CLI regression tests; standard library only."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

exe, fixtures = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
def run(*args, code=0):
    result = subprocess.run([str(exe), *map(str, args)], capture_output=True)
    assert result.returncode == code, (args, result.returncode, result.stderr.decode(errors='replace'))
    return result.stdout

with tempfile.TemporaryDirectory(prefix='tracelens-cli-') as tmp:
    tmp = Path(tmp)
    trace = tmp / 'run.strace'
    trace.write_bytes((fixtures / 'mixed.strace').read_bytes())
    before = tmp / 'before.json'
    run('inspect', trace, '--format', 'json', '--output', before)
    snap = json.loads(before.read_bytes())
    assert snap['calls'] == '7' and snap['schema'] == 'tracelens.snapshot/v1'
    assert snap['sources'][0]['sha256'] == hashlib.sha256(trace.read_bytes()).hexdigest()
    assert snap['syscalls']['read']['total_ns'] == '20000'
    assert snap['slow'][0]['syscall'] == 'fork'
    assert run('inspect', trace, '--format', 'json') == before.read_bytes()
    events = [json.loads(x) for x in run('events', trace, '--syscall', 'read').splitlines()]
    assert events[0]['start']['line'] == '3' and events[0]['end']['line'] == '5'
    assert events[-1]['schema'] == 'tracelens.events-end/v1' and events[-1]['emitted'] == '1'
    errno = [json.loads(x) for x in run('events', trace, '--errno', 'ENOENT').splitlines()]
    assert errno[0]['paths'] == ['/tmp/missing']
    capped = [json.loads(x) for x in run('events', trace, '--limit', '1').splitlines()]
    assert len(capped) == 2 and capped[-1]['output_truncated'] is True
    assert b'openat' in run('source', trace, '--line', '1', '--context', '0')
    assert b'resumed' in run('source', before, '--line', '5', '--context', '0')
    run('source', before, '--line', '999999', code=2)
    for alias in (trace, tmp / 'hard', tmp / 'sym'):
        if alias.name == 'hard': os.link(trace, alias)
        if alias.name == 'sym': alias.symlink_to(trace)
        run('inspect', trace, '--output', alias, code=2)
        run('events', trace, '--output', alias, code=2)
    run('source', before, '--output', trace, code=2)
    run('diff', before, before, '--output', trace, code=2)
    run('inspect', trace, trace, code=2)
    assert trace.read_bytes() == (fixtures / 'mixed.strace').read_bytes()
    same = json.loads(run('diff', before, before))
    assert all(row['delta']['count'] == '0' for row in same['axes']['syscalls'])
    assert same['pid_matching'] is False
    changed = dict(snap)
    changed['schema'] = 'tracelens.snapshot/v99'
    bad = tmp / 'bad.json'; bad.write_text(json.dumps(changed))
    run('diff', before, bad, code=2)
    bad.write_bytes(before.read_bytes().replace(b'"calls": "7"', b'"calls": "7", "calls": "8"', 1))
    run('diff', before, bad, code=2)
    changed = json.loads(before.read_bytes()); changed['syscalls']['read']['count'] = '-1'
    bad.write_text(json.dumps(changed)); run('diff', before, bad, code=2)
    bad.write_bytes(b'{  '); run('diff', before, bad, code=2)
    trace.write_text('open("changed", O_RDONLY) = 3 <0.000050>\n')
    run('source', before, '--line', '1', code=2)
    after = tmp / 'after.json'; run('inspect', trace, '--format', 'json', '--output', after)
    diff = json.loads(run('diff', before, after))
    added = next(row for row in diff['axes']['syscalls'] if row['key'] == 'open')
    removed = next(row for row in diff['axes']['syscalls'] if row['key'] == 'read')
    assert added['delta']['count'] == '1' and removed['delta']['count'] == '-1'
    run('inspect', fixtures / 'partial.strace', '--strict', code=3)
    run('inspect', trace, '--max-bytes', '4', '--strict', code=3)
    run('inspect', trace, '--max-events', '0', code=2)
    # Invalid UTF-8 bytes survive reversible C escaping instead of replacement characters.
    trace.write_bytes(b'open("\xff", O_RDONLY) = 3\n')
    raw = run('events', trace)
    assert json.loads(raw.splitlines()[0])['paths'] == ['\\xff'] and b'\xef\xbf\xbd' not in raw
    trace.write_text('close(3) = 0 <18446744073.709551615>\nclose(4) = 0 <0.000001>\n')
    overflow = json.loads(run('inspect', trace, '--format', 'json', '--strict', code=3))
    assert overflow['syscalls']['close']['total_ns'] == '18446744073709551615'
    # Streaming large synthetic evidence remains deterministic and bounded in retained output.
    trace.write_bytes(b'close(3) = 0 <0.000001>\n' * 20000)
    large = json.loads(run('inspect', trace, '--format', 'json', '--top', '2'))
    assert large['calls'] == '20000' and len(large['slow']) == 2
    eventfile = tmp / 'events.jsonl'; run('events', trace, '--output', eventfile, '--limit', '2')
    assert len(eventfile.read_bytes().splitlines()) == 3
print('CLI evidence, schema, alias and streaming checks passed')
