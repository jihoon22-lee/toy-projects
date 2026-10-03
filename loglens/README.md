# LogLens

LogLens is a log viewer, parser, and investigation workbench for Linux. A
Qt-free core turns raw log bytes into structured records with honest
diagnostics, a console tool exposes filtering and streaming, and a Qt Widgets
shell adds tailing, search, triage state, bookmarks, annotations, timeline
comparison, and byte-preserving export.

## Build and test

```sh
cmake -S . -B build/gui -DCMAKE_BUILD_TYPE=Release
cmake --build build/gui --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir build/gui --output-on-failure
```

LogLens targets Qt 6.

## Parser and GUI contract

LogLens keeps the source bytes of every record in `LogRecord::raw`.  Parsing is
best effort and never treats malformed input as permission to discard evidence:
the structured fields are populated when they are trustworthy, while
`parse_status` and `diagnostics` explain what could not be trusted.

## Record outcomes

`LogRecord::parse_status` has four values:

- `Parsed`: the selected format and all fields that were present were valid.
- `Partial`: the syntax was valid, but a field was missing, had an unexpected
  type, or a timestamp was not usable.
- `Invalid`: the input could not be parsed as the selected structured format.
  The whole source line is retained as the message as well as in `raw`.
- `Unstructured`: auto-detection intentionally treated a free-form line as a
  plain message.

Each `ParseDiagnostic` contains a stable code, optional field name, byte offset
in the original line, and human-readable detail.  Callers should render these
diagnostics instead of reimplementing parser heuristics.  The helper functions
`parseStatusName()` and `parseDiagnosticCodeName()` provide stable display
labels for CLI/GUI adapters.

## JSON Lines

JSON Lines input is validated as one complete JSON object.  The parser accepts
the standard string escapes and decodes `\\uXXXX`, including valid UTF-16
surrogate pairs, to UTF-8.  It rejects truncated strings, invalid escapes,
unpaired surrogates, unescaped control bytes, malformed numbers, trailing
values, and non-object top-level values with explicit diagnostics.  Duplicate
root keys are reported and the first value is retained so an attacker cannot
silently replace a previously interpreted field.  Unescaped source bytes are
also validated as UTF-8; invalid sequences are rejected while the original
bytes remain available in `raw`.

Unknown nested objects and arrays are syntax-checked but are not copied into a
`LogRecord`; this keeps memory bounded.  Nesting is capped at 32 levels,
objects at 256 members, strings at 256 KiB, and a direct JSON line at the
project-wide 1 MiB record limit.  `RecordAssembler` applies its own smaller
per-record cap before invoking the parser, and always reports omitted source
bytes separately.

The supported fields are `ts`, `level`, `logger`, and `msg`; `message` remains
an alias for `msg`.  Scalar `msg` values retain their old display behaviour
while producing an `invalid-field-type` diagnostic.  This is useful for
triaging mixed JSON logs without losing a numeric or boolean payload.

## ISO timestamps

ISO timestamps require a complete date, time, and timezone.  UTC (`Z`) and
signed `+HH:MM`/`+HHMM` offsets are accepted; calendar dates, clock ranges,
fraction length (one to nine digits), and offset hour/minute ranges are
validated before conversion.  Offset errors use the dedicated
`invalid-timestamp-offset` diagnostic so a malformed timezone is not confused
with a missing timestamp.

The parser is dependency-free and compiled as part of `loglens_core`, so the
same contract is exercised by the CLI, background loader, and Qt6 GUI.
Epoch conversion uses checked integer civil-date arithmetic rather than
platform-specific `timegm()`/`mktime()` behavior; pre-epoch and unrepresentable
values are rejected explicitly.

## RFC3164 syslog

The supported syslog shape is `Mon DD HH:MM:SS host component: message`.  RFC3164
does not carry a year or timezone, so a valid syslog record intentionally keeps
`timestamp_ms == 0`; LogLens does not guess either value from the local clock.
The month, day (1–31), time (`00:00:00`–`23:59:59`), host (at most 255
non-whitespace bytes), and component (a nonempty name of at most 32 bytes plus
one trailing colon) are validated before they are promoted to structured
fields.  A missing or malformed required token makes an explicitly selected
syslog record `Partial` and adds a bounded diagnostic; the original line stays
available in `raw` and malformed components are not copied into `source`.

## Format plugins

`--format-plugin FILE` loads a declarative `loglens.format/v1` document so a
custom line shape flows through the same record pipeline as the built-in
formats — multiline folding, record byte limits, filters, and sessions behave
identically.  The plugin document names one ECMAScript regular expression
whose numbered capture groups map onto record fields:

```json
{"kind":"loglens.format/v1","name":"myapp",
 "pattern":"^(\\S+) \\[(\\w+)\\] ([^:]+): (.*)$",
 "fields":{"timestamp":1,"level":2,"source":3,"message":4}}
```

`message` is required; the other fields are optional group numbers (1–32).
Captured timestamps go through the same ISO validator as the built-in parser
and captured levels through the same case-insensitive level table, so an
invalid token produces a `Partial` record with a bounded diagnostic instead
of a silent guess.  A line the pattern does not match stays `Unstructured`
with the raw bytes preserved — plugin records and unparseable lines can be
filtered, exported, and diffed side by side.  Documents are bounded to 4 MiB
and validated strictly: an unreadable file, malformed JSON, an unknown
`kind`, a bad expression, or a field mapping outside the pattern's capture
groups is a load error, never a partial plugin.

## Persistent profiles and saved queries

The core persistence API in `include/loglens/persistence.hpp` stores source
profiles, filter queries, and investigation sessions without depending on the
GUI.  A missing optional file is a successful empty load (`found == false`);
a present file must match its complete versioned schema.  The current schemas
are `loglens.source-profiles/v1`, `loglens.saved-queries/v1`, and
`loglens.session/v1`.

Source profiles use the exact shape below.  `format` is one of the canonical
values `auto`, `iso`, `syslog`, `jsonl`, or `raw`; `multiline` is either
`fold-continuations` or `separate-lines`.

```json
{"schema":"loglens.source-profiles/v1","profiles":[
  {"name":"service","format":"jsonl","multiline":"fold-continuations","max_record_bytes":65536}
]}
```

Saved queries have the same strict root shape and reuse `Filter::parse()` for
validation, so a query loaded from disk has exactly the same syntax and match
semantics as a CLI or GUI query.

```json
{"schema":"loglens.saved-queries/v1","queries":[
  {"name":"timeouts","expression":"level>=WARN AND message~timeout"}
]}
```

An investigation session (`loglens.session/v1`) bundles the source and the
active query of one investigation into a single document, so a run can be
saved and reproduced later.  Only `source.path` is required; `format`,
`multiline`, `max_record_bytes`, `name`, `filter`, and `level` default the
same way as the console flags.  `level` accepts the same spellings as
`--level`, and `filter` reuses `Filter::parse()` validation.  Source paths
are bounded to 4096 UTF-8 bytes.

```json
{"schema":"loglens.session/v1","name":"billing outage",
 "source":{"path":"/var/log/app.log","format":"syslog",
           "multiline":"fold-continuations","max_record_bytes":65536},
 "filter":"level>=WARN AND message~timeout","level":"warn"}
```

The console tool consumes the same document: `--session FILE` loads a session
and fills in any option the command line did not set explicitly, while
`--save-session FILE` writes the effective options as a session before
scanning.  A missing `--session` file or an invalid session fails the run.

```sh
loglens app.log --level ERROR --save-session errors.session.json
loglens --session errors.session.json
loglens --session errors.session.json --level INFO   # flag overrides session
```

The GUI reads and writes the same session document.  The source bar offers
**Open session…**, which restores the source, format, multiline, and record
limit controls, applies any saved `filter`/`level` as the filter expression,
and reopens the file; **Save session** writes the current source and filter
as a session document.

Object fields and JSON keys are strict: unknown or duplicate fields, duplicate
names, malformed JSON, invalid enum/number values, invalid filter expressions,
and an unknown schema version are rejected without returning partial data.
Profiles and queries are sorted by name when saved and loaded, making the
serialized bytes and returned order deterministic.  Files are bounded to
4 MiB, each collection to 128 items, names to 128 UTF-8 bytes, and query
expressions to the filter parser's 4096-byte limit.  Profile record limits are
between one byte and the parser's 1 MiB maximum.

Saves validate and serialize completely before writing a same-directory
temporary file, then atomically replace the destination.  On POSIX, source
reads and writes are descriptor-relative: parent directories and source files
are opened without following the final symlink, reads are bounded from the
opened descriptor, and replacement uses `renameat()`; temporary files use
exclusive `openat()` creation.  On Windows the final source handle is opened
with reparse-point no-follow flags and read with the same bounded-size check;
the fallback uses exclusive CRT creation and `MoveFileExW()` replacement.
Symlink/special-file destinations are refused; no fsync-level durability is
claimed.  Schema versions are
intentionally not migrated implicitly: callers must handle
`UnsupportedVersion` and choose an explicit migration policy.

## Qt GUI profile and query workflow

`loglens-gui` loads the two stores when the window starts.  With the default
constructor, files live below Qt's per-user `AppConfigLocation` as
`source-profiles.json` and `saved-queries.json`; embedding code and tests can
pass `MainWindowOptions::sourceProfilesPath` and
`MainWindowOptions::savedQueriesPath` to select different stores.

The source-profile row exposes the profile name, format, multiline policy,
and bounded maximum record size (1 byte through 1 MiB).  **Save profile**
validates and atomically writes the complete collection; **Apply profile**
reloads the currently open source with those parser settings.  A profile is
also applied to the next **Open log** operation.  The saved-query row lists
loaded names, **Save query** stores the current filter expression, and
**Apply query** copies the selected expression into the filter editor before
applying it.  Applying a persisted query therefore uses the same parser and
filter semantics as typing it manually.

The GUI keeps at most 128 profiles and 128 queries in memory, mirrors the
core 4 MiB store and 4,096-byte query bounds, and refuses a 129th item before
mutating the visible list.  Missing stores are treated as empty; malformed,
unsupported, or invalid stores remain empty and are reported in the status
line with an error code and byte offset.  Failed saves and failed filter
applications leave the previous store or active filter intact, so an input
mistake cannot blank a working investigation.

The focused `test_gui_persistence` QtTest covers load/save/apply, parser
settings reaching the background worker, malformed-store diagnostics, and
the item limits on Qt 6.

## Investigation workbench

The GUI includes a separate **Investigation** dock for preserving evidence and
turning a suspicious time range into a reproducible comparison.  The dock is
backed by the same core objects used by the non-GUI tests; it does not parse a
second, GUI-specific representation of a record.

### Triage state and highlighting

Triage state is stored as the strict, versioned `loglens.triage/v1` document:

```json
{"schema":"loglens.triage/v1","rules":[
  {"name":"Timeout","pattern":"timeout","whole_line":false,
   "priority":40,"style":"#ffcc00"}
],"entries":[
  {"source_path":"/var/log/service.log","line_number":42,
   "bookmarked":true,"annotation":"check upstream retry"}
]}
```

The **Highlights** tab supports literal byte-ranged spans or whole-row
highlighting, priority ordering, safe named/hex colour values, create/update,
delete, and reorder.  `loglens.triage/v0` rule-only files are accepted as an
explicit legacy input and are marked as migrated; the next successful save
writes v1.  Empty or malformed stores never replace the last valid in-memory
state.

The state is bounded before it is parsed or written: at most 128 rules and
8,192 source-line entries, 1,024 bytes per pattern, 4,096 bytes per
annotation, 4,096 bytes per source path, and 64 bytes per style.  Rule names,
source/line identities, and entry contents are validated strictly, including
duplicate identities and unbookmarked empty entries.  Saves use the existing
same-directory atomic persistence backend.

### Record evidence and export

The **Record** tab shows the selected source path and physical line, timestamp,
level, parser status, input/omitted byte counts, every parse diagnostic, the
parsed message, and the original `LogRecord::raw` bytes.  A selected row can be
bookmarked and annotated against its source/line identity.  **Export selected…**
creates a compact `loglens.selection/v1` JSON document with the source path and
one object per selected record.  Each object includes parsed fields,
diagnostics, triage state, byte accounting, and base64 copies of the raw,
message, and source bytes.  The byte fields keep the original evidence available
even when malformed UTF-8 cannot be rendered as a normal `QString`.  The display
strings are UTF-8-normalized conveniences, not the lossless representation.
Export is sorted by visible row, streams one record at a time, stops at 16 MiB,
and is committed atomically. It also refuses to replace the currently open
source log. Empty selections, output-size violations, and failed destinations
leave the source view unchanged.

### Timeline range comparison

The timeline accepts a left-click/drag selection over timestamp buckets and a
right-click (or **Clear range**) to remove it.  Ranges are half-open
`[begin_ms, end_ms)`, so a record exactly at the end boundary belongs only to
the following range.  The selected range is composed with the current
structured filter and raw-text search; clearing it restores the same filtered
view rather than resetting the investigation.

The **Compare** tab stores two selected ranges as baseline and comparison and
computes deterministic, measured signals over the visible records:

- `new-pattern` for a level, source, or normalized message pattern absent from
  the baseline;
- `rate-spike` when an existing key has at least two comparison records and its
  per-minute rate is at least twice the baseline rate; and
- raw correlation groups for `correlation_id`, `request_id`, `thread_id`, and
  `thread` values present in the comparison window.

Every signal and correlation keeps counts, rates where applicable, a stable
explanation, and its first/last physical source line.  Results are sorted by
score and then by dimension/key for reproducibility.  Activating a result
navigates back to the corresponding table row; if that row has been evicted by
bounded storage or filtered out, the status line explains that the evidence is
outside the current visible range.  These heuristics describe measured changes
and intentionally do not claim a diagnosis.

### Verification and current boundary

The focused investigation tests cover timeline mouse interaction, UTF-8
highlight rendering (including byte-to-UTF-16 offset conversion), triage
CRUD/migration and persistence, bookmark/annotation display, byte-preserving
export, diagnostic rendering, comparison navigation, and empty/error paths.
The focused suite is `18/18` under Qt6, and the native TSan partition is
`41/41 PASS`.

## loglens 스트림 계약

`loglens`의 CLI와 GUI는 같은 `RecordAssembler`를 사용한다. `FileTailer::pollChunk`가
poll에서 읽은 raw bytes와 source generation을 전달하고, assembler가 다음 상태를 한 곳에서
보존한다.

POSIX에서 `FileTailer`는 경로의 timestamp가 아니라 열린 파일 handle의 device/inode를
비교한다. 따라서 같은 경로가 더 작거나, 같은 크기이거나, 더 큰 파일로 원자적 rename
교체되어도 `Replaced`로 감지하며, 같은 inode에 쓰는 in-place 축소는 별도의 `Truncated`로
구분한다. source 계층은 missing, permission denied, open/stat/read failure와 unsupported
file type을 typed error로 보존한다.

GUI는 follow 중 발생한 source 오류를 retryable/fatal로 나눈다. missing, permission, open,
stat, read 계열의 retryable 오류에서는 마지막으로 정상적으로 읽은 행을 유지하고 follow
checkbox와 poll timer를 켠 채 `Follow waiting (attempt N)` 상태를 표시한다. 경로에 새 파일이
다시 나타나 새 identity가 확인되면 이전 generation의 assembler와 모델을 비우고 새 파일의
첫 행부터 표시한다. 사용자가 대기 중 Follow를 끄면 retry timer도 멈추며, 다시 켜면 같은
경로를 명시적으로 재개한다. 디렉터리 같은 fatal unsupported file type은 follow를 중지하지만
마지막으로 읽은 화면은 유지하여 사용자가 원인을 확인하거나 다른 파일을 선택할 수 있게
한다. 최초 open 자체가 실패한 경우에는 기존 source를 비우고 Follow를 끄는 기존 계약을
유지한다.

- newline을 기준으로 한 physical line 번호
- 다음 poll에서 이어 붙일 partial bytes
- stack-trace continuation을 확장할 pending record
- truncation/restart 뒤 초기화되는 generation
- 선택된 format과 byte-preserving encoding/error policy

새 record는 `Append`, 기존 record의 continuation은 `Extend` delta로 구분된다. 따라서 GUI는
이미 표시한 행을 갱신하고 CLI는 같은 결과 벡터를 갱신한다. newline 없는 마지막 조각은
follow 모드에서는 보류하며, one-shot CLI의 명시적 EOF `flush()`에서만 record가 된다.

## loglens bounded storage

GUI와 CLI는 기본 8,192개(최대 1,000,000개)의 같은 bounded record store를 사용한다.
GUI status는 visible/retained/seen/dropped, oldest-newest physical line과 capacity를 표시하고,
CLI는 `--capacity N`으로 보존량을 정하며 일반 출력과 `--stats` 모두 같은 요약을 출력한다.
오래된 record가 제거돼도 assembler의 absolute ID는 바뀌지 않아 continuation update가 다른
행에 적용되지 않는다.

source read는 한 poll당 기본 1 MiB(최대 16 MiB)로 제한된다. GUI는 초기 backlog를 이벤트
루프에 나눠 처리하고, one-shot CLI는 최초 file-size snapshot까지만 읽는다. newline 없는
거대한 line이나 continuation은 기본 64 KiB(최대 1 MiB)에서 잘리며, UI/CLI에 정확한
`omitted_bytes`가 표시된다. 이 slice는 이벤트 루프를 독점하는 전체 파일 read를 없앤 기반
단계다.

초기 GUI 로드는 `Latest records`(Tail N)와 `From start` 중에서 선택한다. Tail N은
continuation line을 포함한 logical record의 시작 offset을 bounded byte scan으로 찾고,
선택된 suffix를 실제 `FileTailer`와 `RecordAssembler`로 읽어 원래 physical line number를
유지한다. From start는 첫 poll의 file-size snapshot까지만 읽는다. 두 경로 모두 source
identity를 다시 확인해 선택 시점과 로드 시점이 다른 파일이면 섞어 표시하지 않고 retryable
오류로 끝낸다.

초기 I/O와 parsing은 `LogLoadWorker`가 전용 `QThread`에서 소유하고, `LogModel`과 모든
위젯 변경은 GUI thread에서만 수행한다. worker는 한 번에 최대 512개의 `RecordDelta`만
`LoadBatch`로 발행하며, GUI가 같은 `job_id`와 `sequence`를 acknowledge하기 전에는 다음
batch를 읽거나 발행하지 않는다. 새 파일을 열면 thread-safe job selector가 이전 작업을
취소하고, GUI는 stale job 또는 sequence가 어긋난 batch를 적용하지 않는다. 따라서 느린
파일 I/O가 event loop를 막지 않으면서도 queued signal이 무한히 쌓이지 않는다.

`Follow`는 초기 backlog가 drain되는 동안에도 명시적으로 켜고 끌 수 있다. 취소 또는 Follow
중지는 pending follow poll을 버리고, 재개할 때 현재 source generation부터 다시 읽는다.
구조화된 filter와 대소문자 구분 없는 raw-text search는 worker가 계속 batch를 보내는 중에도
GUI thread에서 안전하게 바꿀 수 있으며, timeline 갱신은 debounce된다.

## loglens filter contract

구조화된 filter는 `level >= WARN`, `source == api`, `source ~ gateway`,
`message ~ "request timeout"`, `message !~ "health"`와 `AND`/`OR`/`NOT`/괄호 조합을
지원한다. `~`와 `!~`는 정규식이 아닌 대소문자 구분 없는 literal substring 연산이다.
따옴표 안에서는 `\"`와 `\\`만 escape로 해석하며, 나머지 UTF-8 바이트는 그대로 보존한다.

한 query는 입력 4,096 bytes, AST 256 nodes, decoded literal 1,024 bytes, nesting depth 64로
제한된다. malformed 또는 제한 초과 query는 `ParseError::position`과 새
`ParseError::end`가 가리키는 UTF-8 입력의 half-open byte range 및 deterministic message로
거부된다. GUI filter 상태도 이 byte range를 표시하며, 이전에 적용된 정상 filter 화면은
오류가 나도 유지한다. CLI도 같은 `[begin,end)` byte range를 stderr에 출력한다.

CLI의 --level shorthand와 --filter는 각각 독립적으로 parse한 뒤 결합하므로 오류 range가
생성된 conjunction의 prefix/괄호 때문에 이동하지 않고 사용자가 입력한 argument에 매핑된다.
GUI도 입력을 trim해서 버리지 않고 untrimmed UTF-8 bytes를 parser에 전달한다. depth 제한 오류는
허용 한도를 넘긴 추가 NOT/괄호 nesting token을 가리키며, unsupported escape가 multibyte UTF-8
scalar를 시작하면 backslash부터 scalar 전체를 range에 포함한다. 실패한 apply는 이전에 적용된
정상 filter를 계속 유지한다.

2026-08-31에 canonical 1 GiB synthetic log(정확히 1,073,741,824 bytes, 1,000,000 records,
SHA-256 `11186d3021e558c8ed5e33473198a6f9f281ca0605ae79739a928a87156435bb`)의 전체 sweep을
완료했다. capacity `8192, 16384, 32768, 65536, 131072, 262144`를 각 3회, process timeout
180초로 실행했으며, 두 Qt major에서 `8192..65536`이 모든 correctness·성능·RSS budget을
만족했다. `131072`은 core RSS, `262144`는 core와 GUI RSS budget을 넘겼다.

고정한 budget은 first result `≤ 5000 ms`, first paint `≤ 5000 ms`, 전체 load `≤ 60000 ms`,
throughput `≥ 25 MiB/s`, records `≥ 25000 records/s`, core peak RSS `≤ 256 MiB`, GUI peak
RSS `≤ 512 MiB`다. 두 Qt 결과에서 best median load time 대비 10% 이내인 가장 작은 적격
capacity를 선택하는 규칙으로 기본 capacity를 `8192`로 결정했다. 대표적인 capacity 8192
median은 다음과 같다.

| Qt | component | first result | first paint | load | throughput | records/s | peak RSS |
|---|---|---:|---:|---:|---:|---:|---:|
| 5 | core | 3.041 ms | — | 1510.632 ms | 677.862 MiB/s | 661974.809 | 24.465 MiB |
| 5 | GUI | 18.031 ms | 19.616 ms | 17717.171 ms | 57.797 MiB/s | 56442.421 | 53.336 MiB |
| 6 | core | 3.049 ms | — | 1480.219 ms | 691.790 MiB/s | 675575.761 | 24.469 MiB |
| 6 | GUI | 18.055 ms | 18.843 ms | 18490.615 ms | 55.379 MiB/s | 54081.488 | 55.980 MiB |

#### 1 GiB benchmark 재현 (opt-in)

benchmark target은 기본 빌드에 포함하지 않는다. Qt 6의 local 실행은 다음과 같다.

```bash
cd loglens
cmake -S . -B build/benchmark-qt6 -DCMAKE_BUILD_TYPE=Release \
  -DLOGLENS_BUILD_BENCHMARKS=ON
cmake --build build/benchmark-qt6 --parallel \
  --target loglens-bench-generate loglens-bench-core loglens-bench-gui
QT_QPA_PLATFORM=offscreen python3.10 benchmarks/run_benchmark.py \
  --build-dir build/benchmark-qt6 \
  --scratch /tmp/loglens-benchmark-qt6 \
  --artifact-dir /tmp/loglens-benchmark-artifacts/qt6 \
  --qt-major 6 \
  --bytes 1073741824 --records 1000000 \
  --capacities 8192,16384,32768,65536,131072,262144 \
  --repetitions 3 --timeout-seconds 180
```

runner는 generator 결과의 정확한 byte/record 수와 SHA-256을
검증한 뒤 core/GUI raw sample을 집계한다. `summary.json`, `summary.md`, `toolchain.json`,
`toolchain.txt`, `samples/*.json`만 artifact로 남기며 1 GiB input과 process log는 scratch에
둔다. `.github/workflows/loglens-benchmark.yml`은 `workflow_dispatch`와
주간 schedule에서만 실행되고 일반 PR에는 포함하지 않는다.

일반 PR에는 별도로 `.github/workflows/ci.yml`의 benchmark smoke가 포함된다. 이것은
1 MiB/1,000 records, capacity `64,256`, 1회, 30초 timeout의 Qt6 harness correctness run이며
budget을 건너뛰고 결과 artifact만 업로드한다. 비용이 큰 1 GiB budget sweep은
opt-in/nightly workflow에 남긴다.
