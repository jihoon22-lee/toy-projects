# toy-projects CI Merge Gate 강화

## Overview

`toy-projects`의 CI가 프로젝트를 정적으로 나열하거나 report 게시 결과를
사용자 관점에서 확인하지 않아도 통과할 수 있던 공백을 보완했다. 이제 하나의
manifest가 `ici verify`와 Qt GUI matrix를 함께 만들고, PR report job은 matrix의
종료 결과 뒤에 실행되어 sticky 댓글과 실제 GitHub Pages HTML까지 검증한다.

구현 커밋은 `a0ca49d` (`chore(ci): enforce manifest and report merge gates`)이며,
이 worktree에서는 원격 push, PR 생성, merge를 수행하지 않았다.

## Context

기존 workflow에는 `diskmap`과 `loglens`가 각각 verify/GUI matrix에 하드코딩되어
있었다. 새 프로젝트를 추가하면서 한 matrix를 갱신하지 않아도 CI가 모르는 채로
지나갈 수 있었고, `report-pr`는 verify만 dependency로 기다렸다. 또한 ici의
업로드/댓글 API 호출이 성공해도 댓글에 HTML 링크가 실제로 생겼는지, Pages URL이
HTML을 반환하는지는 gate하지 않았다. 현재 GitHub `main` branch protection도
조회 결과 `404 Branch not protected`로 아직 설정되지 않은 상태다.

## Changes Made

### 1. 단일 project manifest

- 파일: `/home/jihoon/projects/.worktrees/toy-ci-gates/ci/projects.json`
- `diskmap`, `loglens`의 ici 검증 참여 여부와 GUI build system, descriptor,
  binary, smoke 입력을 한 곳에 기록했다.
- 파일: `/home/jihoon/projects/.worktrees/toy-ci-gates/ci/check_manifest.py`
- manifest schema, 이름/경로 안전성, `ici.toml` 존재, GUI descriptor를 검증한다.
- 저장소 최상위에서 `ici.toml`을 가진 프로젝트 집합과 manifest 집합을 비교하여
  누락 또는 임의 추가를 실패시킨다.
- `GITHUB_OUTPUT`에 verify matrix와 GUI matrix JSON을 각각 내보낸다. 향후
  순수 CLI 프로젝트는 `gui.enabled = false`를 명시하면서 verify에는 남을 수 있다.

### 2. Matrix와 Qt GUI smoke

- 파일: `/home/jihoon/projects/.worktrees/toy-ci-gates/.github/workflows/ci.yml`
- `discover` output을 `fromJSON`으로 소비하여 verify와 GUI matrix를 동적으로
  생성한다.
- GUI matrix는 manifest의 `cmake`/`qmake` descriptor로 실제 Release build를
  수행하고 `QT_QPA_PLATFORM=offscreen`에서 실제 실행 파일을 8초 동안 기동한다.
- GUI matrix가 비어 있는 경우 job 자체를 skip하고, 그 경우에만 Merge Gate가
  skipped를 허용한다.
- ici release asset은 기존 계약대로 `v0.6.0`의 `ici.pyz.sha256` 첫 필드와
  다운로드 파일의 SHA-256을 비교한 뒤 실행한다.

### 3. PR report와 stable Merge Gate

- `report-pr`는 `always() && pull_request`이며 `discover`, `verify`,
  `gui-build`가 성공/실패로 종료한 뒤 평가된다.
- repository 단위 `gh-pages` concurrency로 Pages branch 쓰기 경합을 직렬화한다.
- 모든 verify artifact가 manifest와 정확히 일치하고 HTML/JSON 쌍을 갖는지 확인한
  뒤 ici `publish --report-dir`로 순차 게시한다.
- GitHub API에서 최신 `<!-- ici-report -->` sticky 댓글을 읽어 manifest 프로젝트
  수와 동일한 HTML 링크가 정확히 존재하는지 확인하고, 현재 workflow run URL도
  댓글에 포함됐는지 확인한다.
- 각 링크를 실제로 `curl`하여 `text/html` content type과 HTML body를 확인할
  때까지 Pages 전파를 기다린다.
- `Merge Gate`는 discover, 모든 verify matrix leg, 모든 GUI matrix leg와 PR
  report 검증을 요구한다. push에서는 report job이 skip되는 것을 허용한다.
- 파일: `/home/jihoon/projects/.worktrees/toy-ci-gates/README.md`
  - manifest, report, Pages 확인, branch protection required check 계약을
    사용자 문서에 동기화했다.
- 파일: `/home/jihoon/projects/.worktrees/toy-ci-gates/.gitignore`
  - CI Python helper의 `__pycache__`/`*.pyc`를 제외했다.

## Code Examples

### Manifest-driven matrices

```yaml
verify:
  needs: discover
  strategy:
    matrix:
      project: ${{ fromJSON(needs.discover.outputs.projects) }}

gui-build:
  needs: discover
  if: ${{ needs.discover.outputs.gui_count != '0' }}
  strategy:
    matrix:
      project: ${{ fromJSON(needs.discover.outputs.gui_projects) }}
```

### Stable gate policy

```yaml
merge-gate:
  if: ${{ always() }}
  needs: [discover, verify, gui-build, report-pr]
```

The final shell step rejects any failed/cancelled prerequisite; it only accepts a
skipped GUI job when the manifest has zero GUI entries and accepts a skipped report
only for a push event.

## Verification Results

### Workflow and manifest

```text
actionlint -color                         PASS (exit 0)
python3 -m json.tool ci/projects.json     PASS
python3 ci/check_manifest.py               validated 2 project(s), 2 GUI matrix entries
git diff --check                           PASS
```

### Native build and tests

- `loglens`: CMake Release build and CTest `8/8` passed.
- `diskmap`: qmake6 Release build produced CLI, GUI, and six test executables;
  qmake test target was built successfully.
- Both GUI binaries stayed alive through the 8-second offscreen smoke run.

### ici dogfooding

- Released `/home/jihoon/projects/ici/dist/ici.pyz` reported `ici 0.6.0`.
- `loglens`: `ici verify` PASS, TEM `4.08 / 5`, exit `0`.
- `diskmap`: `ici verify` PASS, TEM `4.85 / 5`, exit `0`.

## Limitations and Follow-up

- The repository currently has no `main` branch protection (`gh api .../branches/main/protection`
  returned `404`). An administrator must mark the exact `Merge Gate` check as required;
  the workflow cannot enforce that repository setting by itself.
- The current manifest has two Qt6 GUI projects. Qt5/Qt6 dual-major coverage remains
  the separate T0-5 workstream and should later extend the GUI matrix metadata without
  reintroducing a second project list.
- Live Pages/comment verification was not run from this local worktree because it
  requires a real pull request, write token, and Pages deployment. The workflow now
  fails closed when any of those user-visible contracts is missing.
