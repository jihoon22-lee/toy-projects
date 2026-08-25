# toy-projects

C++17 / Qt6 로 만드는 실사용 데스크톱 도구 모음.
동시에 [ici](https://github.com/jihoon22-lee/ici) 품질 게이트의 **외부 C++ 검증 대상**으로 쓰인다.

## 프로젝트

| 이름 | 설명 | 상태 |
|---|---|---|
| [diskmap](diskmap/) | 디스크 사용량 트리맵 뷰어 | 진행 중 |
| loglens | 로그 뷰어 / 분석기 | 예정 |

## 공통 구조 규칙

모든 프로젝트는 아래 배치를 따른다. ici 가 C++ 를 다루는 방식(순수 g++ 컴파일, moc 없음,
루트 빌드 디스크립터 거부)에 맞추면서 동시에 "로직은 core, Qt 는 얇은 껍데기" 라는 설계를 강제한다.

```
<project>/
├── ici.toml              project.source_dirs = ["src"]
├── src/
│   ├── core/             순수 C++17, Qt 금지 — ici 전 엔진 검증 대상
│   └── main.cpp          CLI 드라이버 (main 은 여기 하나뿐)
├── tests/*.cpp           각자 main() 을 갖는 독립 테스트 바이너리
└── gui/                  Qt6 셸 + 자체 CMakeLists.txt (ici 스코프 밖)
```

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
./gui/build/<app>
```
