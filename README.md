# toy-projects

C++17 / Qt6 로 만드는 실사용 데스크톱 도구 모음.
동시에 [ici](https://github.com/jihoon22-lee/ici) 품질 게이트의 **외부 C++ 검증 대상**으로 쓰인다.

## 프로젝트

| 이름 | 설명 | 상태 |
|---|---|---|
| [diskmap](diskmap/) | 디스크 사용량 트리맵 뷰어 | 코어 + Qt6 GUI |
| [loglens](loglens/) | 로그 뷰어 / 분석기 | 코어 + Qt6 GUI |

## 공통 구조 규칙

모든 프로젝트는 아래 배치를 따른다. ici 가 C++ 를 다루는 방식(순수 g++ 컴파일, moc 없음,
루트 빌드 디스크립터 거부)에 맞추면서 동시에 "로직은 core, Qt 는 얇은 껍데기" 라는 설계를 강제한다.

```
<project>/
├── ici.toml              project.source_dirs = ["src"]
├── include/<project>/    코어의 공개 헤더 — ici 가 -Iinclude 와 그 하위를 넘겨준다
├── src/                  구현. 전부 ici 검증 대상이다
│   ├── main.cpp          CLI 드라이버 (main 은 여기 하나뿐, 테스트에서 자동 제외)
│   └── gui/              Qt6 셸 + 자체 CMakeLists.txt + gui_main.cpp
└── tests/*.cpp           각자 main() 을 갖는 독립 테스트 바이너리
```

헤더를 `include/<project>/` 에 두는 것은 관례이기도 하지만 ici 의 `get_all_cpp_includes()`
가 `include/` 와 그 하위를 `-I` 로 넘겨주기 때문이기도 하다. 덕분에 테스트도
`#include "<project>/foo.hpp"` 로 참조할 수 있다 — 상대 경로가 필요 없다.

### GUI 도 검증 대상이다

Qt 셸을 `src/` 밖에 두면 "검증할 필요 없는 코드" 라는 뜻이 되어버린다. GUI 도 프로젝트
소스이므로 `src/gui/` 에 두고 `ici.toml` 에 두 가지를 선언한다.

```toml
cpp_pkg_config          = ["Qt6Widgets"]   # lint 가 Qt 헤더를 찾을 수 있게
cpp_external_build_dirs = ["src/gui"]      # 바이너리를 만드는 엔진만 건너뛴다
```

`Q_OBJECT` 클래스는 moc 가 생성한 소스 없이는 링크되지 않으므로 `test`/`sanitize`/`build`
는 `src/gui` 를 건너뛴다. 그러나 `lint`·`line`·`complexity`·`dup`·`exception`·`cycle`·
`security` 는 그대로 읽는다 — **링크가 안 된다는 게 그 코드의 품질을 안 봐도 된다는
뜻은 아니다.** GUI 빌드 자체는 CMake 와 CI 의 헤드리스 스모크 실행이 담당한다.

이 구조는 ici v0.5.2 이상이 필요하다.

## CI 리포트

프로젝트마다 `ici verify` 가 따로 돌아 HTML 리포트가 여러 개 나온다. 이걸 PR 에 남기는 방식은
매트릭스 레그가 각자 게시하는 게 아니라 **집계 잡 하나가 전부 모아서** 게시한다.

레그별 게시가 안 되는 이유가 둘 있다. ici 의 sticky 마커는 고정 문자열 하나라 두 번째 게시가
첫 번째 댓글을 덮어쓰고, 병렬 레그가 같은 gh-pages 브랜치에 동시에 쓰면 Contents API 가
요구하는 blob sha 를 두고 경쟁하다 하나가 유실된다.

그래서 레그는 아티팩트만 올리고, `report-pr` 잡이 전부 내려받아
`ici publish --report-dir` 로 **순차 업로드 후 댓글 하나**를 남긴다. 프로젝트별로 행과 링크가
하나씩 붙는다. 그 잡은 PR 소스를 체크아웃하지 않는다 — 실행물은 체크섬 검증된 릴리스 pyz,
게시물은 verify 잡의 아티팩트뿐이라 PR 이 쓰기 토큰에 닿지 않는다.

## 검증

```bash
cd <project>
/mnt/e/projects/ici/dist/ici.pyz verify --report
```

## GUI 빌드

```bash
cd <project>
cmake -S gui -B gui/build -DCMAKE_BUILD_TYPE=Release
cmake --build gui/build -j
./gui/build/<project>-gui [경로]
```

경로를 주면 폴더 선택 대화상자를 건너뛰고 바로 스캔한다. 덕분에
`QT_QPA_PLATFORM=offscreen` 으로 헤드리스 스모크 실행이 가능하고, CI 가 그렇게 쓴다.
