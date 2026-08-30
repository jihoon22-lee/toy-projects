# Qt 셸 테스트와 Qt 버전 탐지 (toy-projects)

> **상태 보정:** 이 문서는
> `2026-08-30-product-portfolio-master-plan.md`의 T0 세부 입력이다. 아래 stateless
> `parseLines(vector<string>)` 구현은 poll마다 line number와 continuation/partial-line state를
> 잃으므로 그대로 구현하지 않는다. 마스터 T0-2의 stateful `RecordAssembler` 계약이 우선한다.
> Qt 5.15는 현재 설치돼 있으므로 미검증으로 남기지 않고 Qt 6 탐색을 명시적으로 비활성화해
> 양 major를 실측한다.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `loglens` 와 `diskmap` 의 Qt 셸에 단위 테스트를 붙여 `loglens` 의 커버리지 임계값을 되돌리고, 빌드가 설치된 Qt 버전을 따라가게 한다.

**Architecture:** 먼저 셸에 있으면 안 되는 순수 로직을 core 로 뺀다 — 그만큼은 Qt 없이 테스트된다. 남은 것은 진짜 Qt 에 묶인 상태 기계(팔로우, 내비게이션)이고 그건 Qt 테스트로 검증한다. 빌드 정의는 Qt 버전을 고정하지 않고 탐지한다.

**Tech Stack:** C++17, Qt (설치된 6 또는 5), CMake/CTest(loglens), qmake/make(diskmap), ici 0.6.0

**Spec:** 이 계획은 대화에서 합의된 설계를 구현한다. 별도 스펙 문서는 없다 — 기존 구조 안에서 끝나는 변경이고, 근거는 각 태스크에 적었다.

## Global Constraints

- **C++17.** 두 프로젝트 모두 유지
- **"로직은 core, Qt 는 얇은 껍데기" 원칙은 폐기됐다.** Qt 를 core 에 쓰는 것을 금지하지 않는다. 무엇을 어디 둘지는 그때그때 판단한다. 이 계획이 `parseLines` 를 core 로 옮기는 이유는 원칙 때문이 아니라 **그것이 파서 로직이고 테스트 대상이기 때문**이다
- **Qt 버전을 고정하지 않는다.** CMake 는 `find_package(QT NAMES Qt6 Qt5 ...)` 후 `Qt${QT_VERSION_MAJOR}::`, qmake 는 `qmake6` 가 없으면 `qmake` 를 쓴다
- **테스트는 프로젝트 루트에서 실행된다.** CMake 는 `WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}`. C-9 계약
- **위젯 테스트는 디스플레이가 필요하다.** 로컬·CI 모두 `QT_QPA_PLATFORM=offscreen`
- **커버리지 임계값을 실측 없이 올리지 않는다.** 측정한 값과 근거를 `ici.toml` 주석에 남긴다
- **검증**: `cd <project> && QT_QPA_PLATFORM=offscreen ../../ici/dist/ici.pyz verify`
- **브랜치**: `feat/qt-shell-tests` 에서 작업하고 PR 로 병합한다. `main` 직접 작업 금지

---

### Task 1: poll 경계를 보존하는 stateful record assembler

**Files:**
- Modify: `loglens/include/loglens/log_parser.hpp`
- Modify: `loglens/src/log_parser.cpp`
- Modify: `loglens/include/loglens/log_source.hpp`
- Modify: `loglens/src/log_source.cpp`
- Modify: `loglens/src/main.cpp`
- Modify: `loglens/src/gui/main_window.cpp`
- Test: `loglens/tests/test_log_parser.cpp`
- Test: `loglens/tests/test_log_source.cpp`

**Consumes:** 기존 `parseLine`, `isContinuation`, `FileTailer`

**Produces:** physical line number, partial bytes와 previous-record extension을 poll 사이에 보존하는
`RecordAssembler` 및 명시적 record delta contract. 최종 이름과 타입은 failing test에서 필요한
상태를 먼저 확인한 뒤 정한다.

익명 `parseLines`를 그대로 core로 옮기면 두 결함을 고정한다. 매 poll마다 line number가 1로
돌아가고, 다음 poll의 stack frame이 이전 poll의 record에 붙지 않는다. `std::getline`이 newline
없는 마지막 조각을 읽은 뒤 offset을 끝으로 옮기는 문제도 함께 다룬다.

- [ ] **Step 1: write failing parser-state tests**
  - initial batch의 두 record가 physical line 1/2를 가진다.
  - 두 번째 `consume`은 line 3부터 계속된다.
  - 첫 poll의 error record와 두 번째 poll의 stack frame이 하나의 record가 된다.
  - source 첫 줄의 continuation은 유실되지 않는다.
  - reset/generation change 뒤 line과 continuation state가 명시된 초기값으로 돌아간다.
- [ ] **Step 2: write failing partial-line tests**
  - newline 없는 `"part"` 뒤 `"ial\n"`이 들어오면 `"partial"` 한 줄만 나온다.
  - partial bytes는 rotation/truncation 때 이전 generation record로 섞이지 않는다.
  - explicit EOF/flush 정책 없이 미완성 조각을 완성 record로 발표하지 않는다.
- [ ] **Step 3: design a delta contract before implementation**
  - 새 record append와 이전 record continuation extension을 구분한다.
  - GUI model과 CLI vector가 같은 delta를 적용할 수 있어야 한다.
  - assembler가 이미 UI에 넘긴 record의 dangling pointer를 보관하지 않는다.
- [ ] **Step 4: implement the smallest stateful core**
  - next physical line, partial bytes, generation과 continuation state를 소유한다.
  - FileTailer는 complete line과 pending bytes의 경계를 보존한다.
  - parser format 선택을 호출마다 임의로 바꾸지 않는다.
- [ ] **Step 5: remove duplicated GUI/CLI folding**
  - GUI 익명 `parseLines`와 CLI `appendLine`을 제거한다.
  - initial open과 follow poll이 같은 pipeline을 사용한다.
- [ ] **Step 6: run native and ici tests**

```bash
cd loglens
cmake -S . -B build/check -DCMAKE_BUILD_TYPE=Debug
cmake --build build/check --parallel
QT_QPA_PLATFORM=offscreen ctest --test-dir build/check --output-on-failure
QT_QPA_PLATFORM=offscreen ../../ici/dist/ici.pyz verify
```

- [ ] **Step 7: commit**

```bash
git commit -m "fix(loglens): preserve parser state across file polls"
```

---

### Task 2: `bucketTotal` 을 `Bucket::total()` 로

**Files:**
- Modify: `loglens/include/loglens/log_stats.hpp:14-17`
- Modify: `loglens/src/log_stats.cpp`
- Modify: `loglens/src/gui/timeline_widget.cpp:9-23`
- Test: `loglens/tests/test_log_stats.cpp`

**Interfaces:**
- Consumes: 기존 `loglens::Bucket`
- Produces: `std::size_t loglens::Bucket::total() const`

`timeline_widget.cpp` 의 익명 네임스페이스에 `bucketTotal(const Bucket&)` 이 있다. `Bucket` 은 core 타입이므로 그 합계는 core 에 있는 편이 자연스럽고, 위젯이 아니라 통계의 성질이다.

- [ ] **Step 1: Write the failing test**

`loglens/tests/test_log_stats.cpp` 에 추가하고 `main()` 의 호출 목록에 넣는다.

```cpp
static void testBucketTotalSumsEveryLevel() {
    loglens::Bucket bucket;
    bucket.level_counts[static_cast<std::size_t>(loglens::Level::Info)] = 3;
    bucket.level_counts[static_cast<std::size_t>(loglens::Level::Error)] = 2;

    CHECK_EQ(bucket.total(), static_cast<std::size_t>(5));
}

static void testEmptyBucketTotalsZero() {
    CHECK_EQ(loglens::Bucket().total(), static_cast<std::size_t>(0));
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd loglens && cmake --build build/check --parallel
```
Expected: 컴파일 실패 — `Bucket` 에 `total` 이 없다

- [ ] **Step 3: Add the accessor**

`loglens/include/loglens/log_stats.hpp` 의 `Bucket` 을 바꾼다.

```cpp
struct Bucket {
    std::uint64_t start_ms = 0;
    std::array<std::size_t, kLevelCount> level_counts{};

    // Records in this bucket across every level. The timeline needs it to size
    // a bar; it is a property of the bucket, not of the widget drawing it.
    std::size_t total() const;
};
```

`loglens/src/log_stats.cpp` 에 정의를 더한다.

```cpp
std::size_t Bucket::total() const {
    std::size_t sum = 0;
    for (std::size_t count : level_counts) {
        sum += count;
    }
    return sum;
}
```

`loglens/src/gui/timeline_widget.cpp` 에서 익명 네임스페이스의 `bucketTotal` 을 지우고 호출부를 `bucket.total()` 로 바꾼다. `kBarColours` 는 남긴다.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cd loglens && cmake --build build/check --parallel && QT_QPA_PLATFORM=offscreen ctest --test-dir build/check --output-on-failure
```
Expected: 8/8 통과

- [ ] **Step 5: Commit**

```bash
git add loglens/include/loglens/log_stats.hpp loglens/src/log_stats.cpp \
        loglens/src/gui/timeline_widget.cpp loglens/tests/test_log_stats.cpp
git commit -m "refactor(loglens): make the bucket total a property of the bucket"
```

---

### Task 3: loglens 팔로우 의미론에 Qt 테스트

**Files:**
- Create: `loglens/tests/test_main_window.cpp`
- Modify: `loglens/CMakeLists.txt`

**Interfaces:**
- Consumes: `loglens_gui`, `MainWindow::openPath(const QString&)`, `MainWindow::pollNow()`
- Produces: 없음

`main_window.cpp`의 팔로우 상태 기계와 source/model lifecycle은 이전에 QtTest로 검증되지
않았고, **기능은 스모크로만 확인했지 규칙은 아무도 검증하지 않았다.**

제품에도 의미가 있는 `MainWindow::pollNow()` 공개 경계를 통해 파일 교체·읽기 오류를
결정적으로 구동한다. 실제 500ms follow timer의 append 경로는 `QTRY_COMPARE`로 확인해
테스트가 고정 sleep에 의존하지 않게 한다.

- [x] **Step 1: Write the failing test**

`loglens/tests/test_main_window.cpp` (아래 코드는 최소 fixture를 설명하는 초기 스케치다;
실제 구현은 public poll seam, stable widget names, stale-state와 timeline render 검증까지
확장했다.)

```cpp
#include <QCheckBox>
#include <QTableView>
#include <QtTest>

#include <fstream>

#include "loglens/gui/main_window.hpp"

class TestMainWindow : public QObject {
    Q_OBJECT

private slots:
    void openingAFileFillsTheTable();
    void growthAppendsRows();
    void truncationResetsTheModel();
    void aReadErrorStopsFollowing();
};

namespace {

// Reaching the widgets by type rather than by name keeps the test off
// MainWindow's private members.
QAbstractItemModel* tableModel(MainWindow& window) {
    auto* table = window.findChild<QTableView*>();
    return table == nullptr ? nullptr : table->model();
}

int rowCount(MainWindow& window) {
    QAbstractItemModel* model = tableModel(window);
    return model == nullptr ? -1 : model->rowCount(QModelIndex());
}

void write(const QString& path, const QString& text, bool append) {
    std::ofstream out(path.toStdString(),
                      append ? std::ios::app : std::ios::trunc);
    out << text.toStdString();
}

// The poll slot is private, which moc still makes invokable. Calling it beats
// waiting on the 500 ms timer: the test asserts the rule, not the schedule.
void poll(MainWindow& window) {
    QVERIFY(QMetaObject::invokeMethod(&window, "pollSource"));
}

QString line(const char* level, int n) {
    return QStringLiteral("2026-08-26T04:15:2%1.000Z %2  [api] request %3")
        .arg(n % 10)
        .arg(QString::fromLatin1(level))
        .arg(n);
}

} // namespace

void TestMainWindow::openingAFileFillsTheTable() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("app.log"));
    write(path, line("INFO", 1) + "\n" + line("WARN", 2) + "\n", false);

    MainWindow window;
    window.openPath(path);

    QCOMPARE(rowCount(window), 2);
}

void TestMainWindow::growthAppendsRows() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("app.log"));
    write(path, line("INFO", 1) + "\n", false);

    MainWindow window;
    window.openPath(path);
    QCOMPARE(rowCount(window), 1);

    write(path, line("ERROR", 2) + "\n", true);
    poll(window);

    // Appended, not reloaded: the tailer keeps its offset, so the first line is
    // not read twice.
    QCOMPARE(rowCount(window), 2);
}

void TestMainWindow::truncationResetsTheModel() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("app.log"));
    write(path, line("INFO", 1) + "\n" + line("INFO", 2) + "\n", false);

    MainWindow window;
    window.openPath(path);
    QCOMPARE(rowCount(window), 2);

    // Rotation: the retained rows no longer correspond to anything on disk, so
    // appending under them would show a file that never existed.
    write(path, line("WARN", 9) + "\n", false);
    poll(window);

    QCOMPARE(rowCount(window), 1);
}

void TestMainWindow::aReadErrorStopsFollowing() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("app.log"));
    write(path, line("INFO", 1) + "\n", false);

    MainWindow window;
    window.openPath(path);
    auto* follow = window.findChild<QCheckBox*>();
    QVERIFY(follow != nullptr);
    QVERIFY(follow->isChecked());

    QVERIFY(QFile::remove(path));
    poll(window);

    // Whatever broke will break again on every tick, so following stops rather
    // than spinning on the error.
    QVERIFY(!follow->isChecked());
}

QTEST_MAIN(TestMainWindow)
#include "test_main_window.moc"
```

`loglens/CMakeLists.txt` 의 `test_log_model` 블록 아래에 더한다.

```cmake
add_executable(test_main_window tests/test_main_window.cpp)
target_link_libraries(test_main_window PRIVATE loglens_gui Qt${QT_VERSION_MAJOR}::Test)
target_include_directories(test_main_window PRIVATE tests)
add_test(NAME test_main_window COMMAND test_main_window WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

실제 구현은 private slot을 reflection으로 호출하는 대신 `MainWindow::pollNow()` 공개
경계를 사용한다. 여기에 failed open의 stale model/timeline과 follow timer 정리, stable
`objectName`/accessibility name, `TimelineWidget`의 빈 상태·색상 막대 렌더링 회귀 테스트를
추가했다. 따라서 이 태스크의 핵심은 4개 fixture 시나리오보다 넓고, Qt 5/Qt 6에서 같은
10개 CTest 목록을 실행한다.

- [x] **Step 2: Run the test to verify it fails**

```bash
cd loglens && cmake -S . -B build/check -DCMAKE_BUILD_TYPE=Debug && cmake --build build/check --parallel && QT_QPA_PLATFORM=offscreen ctest --test-dir build/check --output-on-failure -R test_main_window
```
Expected: 컴파일은 되지만 하나 이상 실패할 수 있다. **실패하면 그것이 발견이다** — 팔로우 규칙 중 하나가 실제로는 다르게 동작한다는 뜻이므로, 코드와 테스트 중 어느 쪽이 틀렸는지 판단하고 고친다.

- [x] **Step 3: Fix whatever the tests found**

실패한 단언마다 원인을 확인한다. `main_window.cpp` 의 동작이 틀렸으면 코드를 고치고, 테스트의 기대가 틀렸으면 테스트를 고치되 **왜 그 기대가 틀렸는지 주석으로 남긴다.** 전부 통과했다면 이 스텝은 변경 없이 넘어간다.

- [x] **Step 4: Run the tests to verify they pass**

```bash
cd loglens && cmake --build build/check --parallel && QT_QPA_PLATFORM=offscreen ctest --test-dir build/check --output-on-failure
```
Expected: 10/10 통과 (Qt6와 Qt5 각각)

- [x] **Step 5: Commit**

```bash
git add loglens/tests/test_main_window.cpp loglens/CMakeLists.txt loglens/src/gui/main_window.cpp
git commit -m "test(loglens): assert the follow rules, not just that it runs"
```

---

### Task 4: diskmap 내비게이션에 Qt 테스트

**Files:**
- Create: `diskmap/tests/test_main_window.cpp`
- Create: `diskmap/tests/test_main_window.pro`
- Modify: `diskmap/tests/tests.pro`

**Interfaces:**
- Consumes: `diskmap_gui`, `MainWindow::scanPath(const QString&)`, private slot `goUp`
- Produces: 없음

내비게이션의 `trail_` 스택은 push/pop 과 버튼 활성 상태가 얽혀 있어 **off-by-one 이 살기 좋은 자리**인데 지금 아무도 안 본다.

스캔이 `QFutureWatcher` 로 비동기라 완료를 기다려야 한다. `QTRY_VERIFY` 가 조건이 참이 될 때까지 이벤트 루프를 돌리며 폴링하므로 고정 대기 없이 결정적이다.

- [ ] **Step 1: Write the failing test**

`diskmap/tests/test_main_window.cpp`

```cpp
#include <QLabel>
#include <QPushButton>
#include <QtTest>

#include "diskmap/gui/main_window.hpp"
#include "diskmap/gui/treemap_widget.hpp"

class TestMainWindow : public QObject {
    Q_OBJECT

private slots:
    void scanningFillsTheBreadcrumbWithTheRoot();
    void activatingADirectoryDescends();
    void goingUpReturnsAndStopsAtTheRoot();
    void activatingALeafDoesNothing();
};

namespace {

// Reaching the widgets by type rather than by name keeps the test off
// MainWindow's private members. The breadcrumb is the only QLabel carrying a
// "/"-joined trail, so it is found by that.
QString breadcrumb(MainWindow& window) {
    for (QLabel* label : window.findChildren<QLabel*>()) {
        if (label->text().contains(QLatin1Char('/')) || !label->text().isEmpty()) {
            return label->text();
        }
    }
    return QString();
}

QPushButton* upButton(MainWindow& window) {
    for (QPushButton* button : window.findChildren<QPushButton*>()) {
        if (button->text().contains(QStringLiteral("Up"), Qt::CaseInsensitive)) {
            return button;
        }
    }
    return nullptr;
}

TreemapWidget* treemap(MainWindow& window) {
    return window.findChild<TreemapWidget*>();
}

// A directory with one subdirectory holding a file, so there is somewhere to
// descend into and something with no children to try descending into.
QString makeTree(QTemporaryDir& dir) {
    QDir root(dir.path());
    root.mkpath(QStringLiteral("big"));
    QFile file(dir.filePath(QStringLiteral("big/payload.bin")));
    file.open(QIODevice::WriteOnly);
    file.write(QByteArray(4096, 'x'));
    file.close();
    return dir.path();
}

} // namespace

void TestMainWindow::scanningFillsTheBreadcrumbWithTheRoot() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MainWindow window;
    window.scanPath(makeTree(dir));

    // The scan runs on a worker thread, so the assertion waits on the result
    // rather than on a fixed delay.
    QTRY_VERIFY(!breadcrumb(window).isEmpty());
    QVERIFY(treemap(window)->currentNode() != nullptr);
}

void TestMainWindow::activatingADirectoryDescends() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MainWindow window;
    window.scanPath(makeTree(dir));
    QTRY_VERIFY(treemap(window)->currentNode() != nullptr);

    const diskmap::FsNode* root = treemap(window)->currentNode();
    QVERIFY(!root->children.empty());
    const QString before = breadcrumb(window);

    emit treemap(window)->nodeActivated(&root->children.front());

    QVERIFY(breadcrumb(window) != before);
    QVERIFY(upButton(window)->isEnabled());
}

void TestMainWindow::goingUpReturnsAndStopsAtTheRoot() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MainWindow window;
    window.scanPath(makeTree(dir));
    QTRY_VERIFY(treemap(window)->currentNode() != nullptr);

    const diskmap::FsNode* root = treemap(window)->currentNode();
    const QString atRoot = breadcrumb(window);
    emit treemap(window)->nodeActivated(&root->children.front());

    QVERIFY(QMetaObject::invokeMethod(&window, "goUp"));
    QCOMPARE(breadcrumb(window), atRoot);
    // At the root there is nowhere to go, and a second Up must not pop past it.
    QVERIFY(!upButton(window)->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(&window, "goUp"));
    QCOMPARE(breadcrumb(window), atRoot);
}

void TestMainWindow::activatingALeafDoesNothing() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MainWindow window;
    window.scanPath(makeTree(dir));
    QTRY_VERIFY(treemap(window)->currentNode() != nullptr);

    const diskmap::FsNode* root = treemap(window)->currentNode();
    const diskmap::FsNode* directory = &root->children.front();
    emit treemap(window)->nodeActivated(directory);
    const QString atDirectory = breadcrumb(window);

    // A file has no children, so descending into it would show an empty view
    // with no way to tell why.
    QVERIFY(!directory->children.empty());
    emit treemap(window)->nodeActivated(&directory->children.front());

    QCOMPARE(breadcrumb(window), atDirectory);
}

QTEST_MAIN(TestMainWindow)
#include "test_main_window.moc"
```

`diskmap/tests/test_main_window.pro`

```qmake
TEMPLATE = app
CONFIG += testcase console
CONFIG -= app_bundle
QT += core gui widgets concurrent testlib

TARGET = test_main_window
INCLUDEPATH += $$PWD/../include $$PWD
SOURCES += test_main_window.cpp
LIBS += -L$$OUT_PWD/../src/gui -ldiskmap_gui -L$$OUT_PWD/../src -ldiskmap_core
```

`diskmap/tests/tests.pro` 의 `SUBDIRS` 에 `test_main_window.pro` 를 더한다.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd diskmap && rm -rf build/check && mkdir -p build/check && cd build/check \
  && qmake6 ../../diskmap.pro && make -j"$(nproc)" \
  && QT_QPA_PLATFORM=offscreen ./tests/test_main_window
```
Expected: 하나 이상 실패할 수 있다. `breadcrumb()` 헬퍼가 엉뚱한 `QLabel` 을 집을 가능성이 가장 높다 — 그러면 라벨을 구별할 방법을 찾아 헬퍼를 고친다(예: `objectName` 을 위젯 쪽에 부여).

- [ ] **Step 3: Fix whatever the tests found**

실패 원인을 확인해 고친다. 위젯을 구별할 수 없어 실패했다면 `main_window.cpp` 에서 `breadcrumb_->setObjectName(QStringLiteral("breadcrumb"))` 과 `upButton_->setObjectName(QStringLiteral("up"))` 을 설정하고 헬퍼를 `findChild<QLabel*>(QStringLiteral("breadcrumb"))` 로 바꾼다. **테스트를 위해 이름을 붙이는 것은 정당하다** — 위젯에 이름이 있으면 디버깅과 접근성 도구에서도 쓸모가 있다.

- [ ] **Step 4: Run every diskmap test**

```bash
cd diskmap/build/check && QT_QPA_PLATFORM=offscreen make check TESTARGS=-xunitxml 2>&1 | tail -20
```
Expected: 7개 바이너리 전부 통과

- [ ] **Step 5: Commit**

```bash
git add diskmap/tests/test_main_window.cpp diskmap/tests/test_main_window.pro \
        diskmap/tests/tests.pro diskmap/src/gui/main_window.cpp
git commit -m "test(diskmap): assert the navigation trail, not just that it draws"
```

---

### Task 5: 설치된 Qt 를 따라가게 한다

**Files:**
- Modify: `loglens/CMakeLists.txt`
- Modify: `loglens/src/gui/CMakeLists.txt`
- Modify: `diskmap/src/gui/treemap_widget.cpp:108,119`
- Modify: `loglens/ici.toml`, `diskmap/ici.toml`
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: Task 1–4
- Produces: 없음

여기는 Qt 6 가 설치돼 있고 내부망은 Qt 5.15 다. 빌드 정의가 버전을 고정하면 같은 소스가 한쪽에서 안 빌드된다.

- [x] **Step 1: Detect the Qt version in CMake**

`loglens/CMakeLists.txt` 의 `find_package` 를 바꾼다.

```cmake
# Whichever Qt the machine has. Development here is Qt 6; the closed network is
# Qt 5.15. Pinning a major version would make the same source unbuildable on one
# of them for no reason.
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Test)
```

같은 파일과 `loglens/src/gui/CMakeLists.txt` 의 `Qt6::Widgets` / `Qt6::Test` 를 `Qt${QT_VERSION_MAJOR}::Widgets` / `Qt${QT_VERSION_MAJOR}::Test` 로 바꾼다. `src/gui/CMakeLists.txt` 의 `find_package(Qt6 REQUIRED COMPONENTS Widgets)` 줄은 루트가 이미 찾았으므로 지운다. CI가 반대 major를 명시적으로 비활성화할 수 있도록 `CMAKE_DISABLE_FIND_PACKAGE_Qt5`와 `CMAKE_DISABLE_FIND_PACKAGE_Qt6`가 설정된 이름은 `REQUIRED NAMES` 목록에서 먼저 제거한다.

- [x] **Step 2: Guard the two Qt 6-only calls**

`QMouseEvent::position()` 은 Qt 6 에서 들어왔고 Qt 5 는 `localPos()` 다. 이 저장소에서 Qt 5 와 어긋나는 곳은 여기 두 줄뿐이다.

`diskmap/src/gui/treemap_widget.cpp` 상단에 헬퍼를 더한다.

```cpp
namespace {

// QMouseEvent::position() arrived in Qt 6; Qt 5 spells the same thing
// localPos(). These two call sites are the only place this repository parts
// ways with Qt 5.
QPointF eventPos(const QMouseEvent* event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->localPos();
#endif
}

} // namespace
```

`mouseMoveEvent` 와 `mousePressEvent` 의 `event->position()` 을 `eventPos(event)` 로 바꾼다. 기존 익명 네임스페이스가 이미 있으면 그 안에 넣는다.

- [x] **Step 3: List both Qt packages for lint**

두 `ici.toml` 의 `cpp_pkg_config` 를 바꾼다. `get_cpp_pkg_config_flags` 는 **해석에 실패한 패키지를 건너뛰므로** 둘을 나열하면 설치된 쪽이 잡힌다.

```toml
# Whichever Qt the machine has. ici skips a package pkg-config cannot resolve,
# so listing both makes the same config work on a Qt 6 desktop and a Qt 5.15
# closed-network box.
cpp_pkg_config = ["Qt6Widgets", "Qt5Widgets"]
```

`diskmap` 은 `Qt6Concurrent` 도 쓰므로 `["Qt6Widgets", "Qt6Concurrent", "Qt5Widgets", "Qt5Concurrent"]` 로 한다.

- [x] **Step 4: Select qmake explicitly per matrix leg**

`.github/workflows/ci.yml` 의 `gui-build` 잡은 diskmap Qt6에 `/usr/bin/qmake6`, Qt5에 `/usr/bin/qmake`를 명시한다. 두 경로 모두 `-query QT_VERSION`의 major를 matrix 값과 비교한 뒤 shadow build를 시작한다. 로컬 문서(`README.md`)의 예시도 같은 경로를 사용한다.

```bash
case "$QT_MAJOR" in
  5) QMAKE=/usr/bin/qmake ;;
  6) QMAKE=/usr/bin/qmake6 ;;
esac
test "$("$QMAKE" -query QT_VERSION | cut -d. -f1)" = "$QT_MAJOR"
"$QMAKE" ../../diskmap.pro && make -j"$(nproc)"
```

- [x] **Step 5: Verify under the installed Qt 6 and Qt 5 toolchains**

```bash
cd loglens && rm -rf build/check && cmake -S . -B build/check -DCMAKE_BUILD_TYPE=Debug \
  && cmake --build build/check --parallel \
  && QT_QPA_PLATFORM=offscreen ctest --test-dir build/check --output-on-failure
```
Expected: 통과, 그리고 configure 로그에 잡힌 Qt 버전이 6 으로 보인다

현재 설치된 Qt 5를 강제로 한 번 더 돌린다. Qt 6이 먼저 발견되지 않게 명시적으로 탐색을
비활성화하고 configure log의 `QT_VERSION_MAJOR`와 실제 link target을 확인한다.

```bash
cd loglens && rm -rf build/qt5 \
  && cmake -S . -B build/qt5 -DCMAKE_BUILD_TYPE=Debug -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON \
  && cmake --build build/qt5 --parallel \
  && QT_QPA_PLATFORM=offscreen ctest --test-dir build/qt5 --output-on-failure
```
diskmap도 `/usr/bin/qmake`가 보고하는 Qt 5.15로 별도 shadow build와 `make check`를 실행한다.

T0-5에서 위 검증을 CI와 같은 네 leg로 재실행했다. `gui-build`는 각 leg에서 선택 major의
pkg-config `Core`, `Gui`, `Widgets`, `Concurrent`, `Test` 버전을 출력·검증하고, CMake는
반대 major disable guard와 `-- <project>: using Qt<major> <version>` configure output 및 최종
`ldd` 링크를 확인한다. qmake는 명시적
경로와 `-query QT_VERSION`을 확인한다. 이어서 CMake는 CTest, qmake는 `make check`, 두
경로 모두 실제 GUI binary의 offscreen smoke를 실행한다. Qt6/Qt5 모두 build/test/smoke가
통과했다.

- [x] **Step 6: Commit**

구현은 `chore/qt5-qt6-matrix` 브랜치의 `c9554a0 ci: exercise the Qt5 and Qt6 GUI matrix`로
커밋했다. 이 bounded 작업의 지시에 따라 push/PR/merge는 수행하지 않고 부모 세션에서
원격 CI를 실행한다.

```bash
git add loglens/CMakeLists.txt loglens/src/gui/CMakeLists.txt \
        diskmap/src/gui/treemap_widget.cpp loglens/ici.toml diskmap/ici.toml \
        .github/workflows/ci.yml README.md
git commit -m "build: follow whichever Qt the machine has"
```

---

### Task 6: 임계값과 문서

**Files:**
- Modify: `loglens/ici.toml`
- Modify: `README.md`
- Modify: `ROADMAP.md`

**Interfaces:**
- Consumes: Task 1–5
- Produces: 없음

- [ ] **Step 1: Measure**

```bash
for p in loglens diskmap; do
  (cd $p && rm -rf build/ici-* && QT_QPA_PLATFORM=offscreen ../../ici/dist/ici.pyz verify \
    2>&1 | grep -E "Threshold|Coverage:Module|Total Engines|Suite:" | sed "s|^|[$p] |")
done
```
실측된 branch·function 커버리지를 적어둔다.

- [ ] **Step 2: Raise the thresholds to what was measured**

`loglens/ici.toml` 의 `[engines.test]` 를 실측값에 맞춰 올린다. 목표는 원래의 `min_branch_cov = 80.0` / `min_func_cov = 90.0` 이다.

**실측이 80/90 을 넘으면** 그 값으로 되돌리고 주석을 바꾼다.

```toml
# 셸에 테스트가 붙어 원래 값으로 돌아왔다. src/gui 가 커버리지에 들어온 뒤
# 55/80 으로 내렸던 것은 코드가 나빠져서가 아니라 분모가 바뀌어서였고,
# 이제 그 분모를 실제로 덮었다.
min_branch_cov = 80.0
min_func_cov = 90.0
```

**넘지 못하면 도달한 값에 여유를 두고, 무엇이 아직 안 덮였는지 파일별로 적는다.** 올리지 못한 것을 올렸다고 적지 않는다.

- [ ] **Step 3: Retire the principle from the docs**

`README.md` 의 "공통 구조 규칙" 에서 "로직은 core, Qt 는 얇은 껍데기" 라는 서술을 지우고 아래로 바꾼다.

```markdown
### Qt 를 core 에 쓰는 것을 금지하지 않는다

한동안 "로직은 core, Qt 는 얇은 껍데기" 를 규칙처럼 지켰지만 그만뒀다. Qt 는 GUI 툴킷이기
전에 프레임워크고, 도메인까지 Qt 로 구현하는 코드베이스가 훨씬 많다. 규칙을 지키는 것 자체가
목적이 되면 코드가 아니라 규칙에 맞추게 된다.

무엇을 어디 둘지는 그때그때 판단한다. 이 저장소에서 `parseLines` 가 core 에 있는 이유는
원칙 때문이 아니라 **그것이 파서 로직이고 테스트 대상이기 때문**이다.

예외가 하나 있고 그건 규칙이 아니라 요구다. [`ici/viewer`](https://github.com/jihoon22-lee/ici/tree/main/viewer)
의 `icirv` CLI 는 Qt 가 없는 RHEL 8 에 정적 링크로 나가므로 그 코어는 Qt 를 쓸 수 없다.
```

"알려진 갭: Qt 셸에 단위 테스트가 없다" 절을 실제 상태로 고친다 — 어느 파일이 덮였고 어느 파일이 아직 아닌지.

- [ ] **Step 4: Update the roadmap**

`ROADMAP.md` 의 "Qt 셸에 단위 테스트가 없다" 절을 결과로 바꾼다. 임계값이 원래대로 돌아왔으면 그렇게, 아니면 남은 수치와 이유를 적는다.

- [ ] **Step 5: Verify both projects and run the full check**

```bash
for p in loglens diskmap; do
  (cd $p && rm -rf build/ici-* && QT_QPA_PLATFORM=offscreen ../../ici/dist/ici.pyz verify \
    2>&1 | grep -E "Total Engines|Suite:" | sed "s|^|[$p] |")
done
```
Expected: 둘 다 `Suite: PASS`

- [ ] **Step 6: Commit, push and open the PR**

```bash
git add loglens/ici.toml README.md ROADMAP.md
git commit -m "docs: retire the Qt-in-core rule, and record what the shell tests cover"
git push -u origin feat/qt-shell-tests
gh pr create --title "test: cover the Qt shells, and follow whichever Qt is installed" --body "..."
```

---

## 자체 검토 결과

**커버리지**

| 합의된 항목 | 태스크 |
|---|---|
| `parseLines` 를 core 로 | Task 1 |
| `bucketTotal` 을 core 로 | Task 2 |
| loglens 셸 테스트 | Task 3 |
| diskmap 셸 테스트 | Task 4 |
| Qt 버전 탐지 + `position()` 가드 | Task 5 |
| 임계값 복구 | Task 6 |
| 원칙 폐기 문서화 | Task 6 Step 3 |

**세 가지를 짚어둔다.**

Task 3 Step 3 과 Task 4 Step 3 은 **조건부 스텝**이다. 셸의 실제 동작을 아직 모르므로, 테스트가 실패하면 그것이 발견이고 코드와 테스트 중 어느 쪽이 틀렸는지 판단해야 한다. 미리 답을 적어두면 그건 추측이다.

**Task 6 Step 2 가 이 계획의 성패다.** 임계값을 되돌리는 게 목적인데, 실측이 못 미치면 되돌리지 못한다. 그 경우 도달값을 적고 무엇이 남았는지 남기는 것까지가 이 태스크다 — **못 올렸는데 올렸다고 적는 것이 유일한 실패다.**

**Qt 5.15와 Qt 6.10이 모두 현재 설치돼 있다.** 두 major의 configure/build/test evidence가
없으면 Task 5는 완료가 아니다. 환경이 나중에 바뀌면 실제 미설치 capability를 report에 남기고,
검증하지 않은 major를 지원 완료로 적지 않는다.

## 이 계획에 없는 것

`ROADMAP.md` 의 **2단계(diskmap 정리 작업대)는 별도 계획**이다. 휴지통을 어떻게 다룰지(`gio trash` 인지 직접 구현인지), 안전 규칙의 범위를 어디까지 볼지 같은 설계 결정이 남아 있어서, 지금 상세 계획을 쓰면 실측이 아니라 추측이 된다. 이 계획이 끝난 뒤 따로 설계한다.
