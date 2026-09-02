# loglens → CMake, diskmap → qmake 구현 계획 (toy-projects)

> **Historical recipe / completed (2026-09-02):** The CMake/qmake migration, Qt shell tests, and
> release-era adapter work described here were completed through the merged implementation PRs.
> The unchecked steps below are preserved as historical design and evidence; do not treat them as
> active work. Current priorities and status are maintained in the [portfolio master plan](2026-08-30-product-portfolio-master-plan.md).

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 두 프로젝트를 각자의 빌드 시스템으로 옮기고, `Q_OBJECT` 클래스를 대상으로 한 단위 테스트를 `tests/` 안에 두어 **실제로 통과시킨다.** 그 과정에서 `loglens` 의 라이브 팔로우 기능을 만든다.

**Architecture:** `loglens` 는 루트 `CMakeLists.txt` 로, `diskmap` 은 루트 `diskmap.pro` 로 전환한다. 두 프로젝트 모두 GUI 를 **라이브러리로 분리**해야 테스트가 링크할 수 있다. ici 는 각 프로젝트 루트의 디스크립터를 보고 CMake/qmake 어댑터를 고른다.

**Tech Stack:** C++17, Qt6 (Widgets, Test), CMake/CTest, qmake/make, ici 0.6.0

**Spec:** [`ici/docs/superpowers/specs/2026-08-29-cmake-qmake-build-adapter-design.md`](https://github.com/jihoon22-lee/ici/blob/main/docs/superpowers/specs/2026-08-29-cmake-qmake-build-adapter-design.md) §4.1, §4.2

## 선행 조건

**ici 0.6.0 이 릴리스되어 있어야 한다.** toy-projects CI 는 `ICI_VERSION` 에 적힌 태그의 릴리스 에셋을 체크섬 검증해 내려받는다(소스 빌드가 아니다). 0.5.5 는 어댑터가 없으므로 이 계획의 Qt 테스트가 CI 에서 통과할 수 없다.

Task 1 을 시작하기 전에 확인한다.

```bash
curl -fsSL -o /tmp/ici.pyz https://github.com/jihoon22-lee/ici/releases/download/v0.6.0/ici.pyz
chmod +x /tmp/ici.pyz && /tmp/ici.pyz --version   # ici 0.6.0
```

## Global Constraints

- **C++17.** 두 프로젝트 모두 `CMAKE_CXX_STANDARD 17` / qmake 기본값을 유지한다
- **로컬 검증은 형제 저장소 배치 기준**: `cd <project> && ../../ici/dist/ici.pyz verify --report`
- **테스트는 프로젝트 루트에서 실행된다.** CMake 는 `add_test(... WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})`, qmake 는 `CONFIG += testcase` 의 기본 동작을 확인해 맞춘다. ici 가 C-9 에서 고정한 계약이다
- **Qt 테스트는 `tests/` 안에 둔다.** ici 가 스캔하지 않는 곳으로 옮기거나 게이트에서 빼지 않는다 — 그것이 이 작업이 없애려는 우회다
- **커버리지 플래그는 주입하지 않는다.** ici 가 configure 시점에 `--coverage` 를 넣는다
- **브랜치**: 각 태스크 묶음마다 `feat/<name>` 브랜치와 PR. `main` 직접 작업 금지
- **진입점(`main()`)은 ici 의 커버리지 스코프에서 제외된다.** `src/main.cpp` 의 커버리지를 올리려 애쓰지 않는다

---

### Task 1: loglens 를 루트 CMake 프로젝트로 전환

**Files:**
- Create: `loglens/CMakeLists.txt`
- Modify: `loglens/src/gui/CMakeLists.txt` (전면 교체)
- Modify: `loglens/ici.toml`

**Interfaces:**
- Consumes: 없음
- Produces: CMake 타깃 `loglens_core`(정적 라이브러리), `loglens_gui`(정적 라이브러리), `loglens`(CLI), `loglens-gui`(GUI), 그리고 `test_<name>` 7개

기능은 바꾸지 않는다. **기존 테스트 7개가 CTest 를 통해 그대로 통과하는지**만 확인하는 단계다. 여기서 실패하면 원인이 전환에 있지 새 코드에 있지 않다.

- [ ] **Step 1: Write the root CMakeLists**

`loglens/CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.16)
project(loglens LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Test)

# The core is Qt-free so it links anywhere; the GUI adds Qt on top.
add_library(loglens_core STATIC
    src/filter_expr.cpp
    src/highlight_rules.cpp
    src/log_parser.cpp
    src/log_record.cpp
    src/log_source.cpp
    src/log_stats.cpp
    src/ring_buffer.cpp
)
target_include_directories(loglens_core PUBLIC include)

add_executable(loglens src/main.cpp)
target_link_libraries(loglens PRIVATE loglens_core)

add_subdirectory(src/gui)

enable_testing()

foreach(name IN ITEMS
        filter_expr highlight_rules log_parser log_record log_source log_stats ring_buffer)
    add_executable(test_${name} tests/test_${name}.cpp)
    target_link_libraries(test_${name} PRIVATE loglens_core)
    target_include_directories(test_${name} PRIVATE tests)
    # Always the project root. tests/ read fixtures by project-relative path,
    # and ici fixed that contract in C-9 — the adapter must not undo it.
    add_test(NAME test_${name} COMMAND test_${name} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endforeach()
```

- [ ] **Step 2: Turn the GUI CMakeLists into a subdirectory that exports a library**

`loglens/src/gui/CMakeLists.txt` 를 통째로 아래로 교체한다. 기존 파일은 독립 프로젝트라 `loglens_core` 를 자기가 또 정의하는데, 루트가 이미 정의하므로 타깃 이름이 충돌한다.

```cmake
# Included from the project root, which already defines loglens_core.
#
# The widgets live in a library rather than only in the executable so tests can
# link them. A Q_OBJECT class needs moc-generated sources, which is exactly what
# ici could not provide before the build adapter existed.
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_library(loglens_gui STATIC
    log_model.cpp
    main_window.cpp
    timeline_widget.cpp
)
target_include_directories(loglens_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(loglens_gui PUBLIC loglens_core Qt6::Widgets)

add_executable(loglens-gui gui_main.cpp)
target_link_libraries(loglens-gui PRIVATE loglens_gui)
```

- [ ] **Step 3: Drop the setting the adapter makes obsolete**

`loglens/ici.toml` 에서 `cpp_external_build_dirs` 줄과 그것을 설명하는 주석 블록을 지운다. **`cpp_pkg_config` 는 남긴다** — `lint` 는 여전히 `-fsyntax-only` 로 Qt 헤더를 직접 파싱한다.

주석을 아래로 바꾼다.

```toml
# The Qt shell is project source like everything else, so it lives under src/
# and every engine reads it. cpp_pkg_config gives lint the Qt include paths it
# needs to parse the widget sources at all.
#
# cpp_external_build_dirs used to sit here too, to keep src/gui out of the test
# link because Q_OBJECT classes need moc-generated sources that a bare g++ call
# cannot provide. ici 0.6.0 drives CMake instead, so moc runs and the setting
# has nothing left to do.
cpp_pkg_config = ["Qt6Widgets"]
```

- [ ] **Step 4: Build and run the tests through CMake directly**

```bash
cd loglens
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel
ctest --test-dir build/manual --output-on-failure
```
Expected: 7/7 통과

- [ ] **Step 5: Verify through ici**

```bash
cd loglens
rm -rf build/ici-cmake
../../ici/dist/ici.pyz verify
```
Expected: `Suite: PASS`. 백엔드 선택 근거가 도구 증거에 남는다. TEM 은 전환 전(4.94)과 **정확히 같지는 않다** — CTest 는 테스트 바이너리를 세므로 7건이고, g++ 경로도 7건이었으므로 카운트는 같지만 커버리지 대상에 `src/gui` 가 새로 들어온다. 어느 쪽이든 임계값(TEM 4.0, branch 80, func 90)을 넘어야 한다.

넘지 못하면 **임계값을 낮추지 말고** 무엇이 새로 분모에 들어왔는지부터 리포트에서 확인한다. `src/gui` 의 위젯 코드는 이 단계에서 테스트가 없으므로 커버리지가 낮은 것이 정상이며, Task 2 가 그중 `log_model.cpp` 를 덮는다. Task 2 이후에도 미달이면 그때 `ici.toml` 의 임계값을 실측에 맞춰 조정하고 **그 근거를 주석으로 남긴다.**

- [ ] **Step 6: Commit**

```bash
git checkout -b feat/loglens-cmake
git add loglens/CMakeLists.txt loglens/src/gui/CMakeLists.txt loglens/ici.toml
git commit -m "build(loglens): drive the build from a root CMakeLists"
```

---

### Task 2: LogModel 증분 추가와 Qt 모델 테스트

**Files:**
- Modify: `loglens/src/gui/log_model.hpp`
- Modify: `loglens/src/gui/log_model.cpp`
- Create: `loglens/tests/test_log_model.cpp`
- Modify: `loglens/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 의 `loglens_gui` 라이브러리 타깃
- Produces: `LogModel::appendRecords(const std::vector<loglens::LogRecord>&)`, `LogModel::resetRecords()`, `LogModel::setFilter(const loglens::Filter*)`(필터를 값으로 보관하도록 변경)

**이 태스크가 A-3 의 실측이다.** `QAbstractItemModelTester` 는 모델의 시그널 계약을 검증하며, 순수 함수로 뽑아낼 수 없다 — 그래서 core 로 우회할 방법이 없고, moc 없이는 링크되지 않는다.

- [ ] **Step 1: Write the failing test**

`loglens/tests/test_log_model.cpp`

```cpp
#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QtTest>

#include <vector>

#include "fake_source.hpp"
#include "log_model.hpp"
#include "loglens/filter_expr.hpp"

using loglens::Level;

namespace {

std::vector<loglens::LogRecord> batch(std::initializer_list<Level> levels) {
    std::vector<loglens::LogRecord> records;
    for (Level level : levels) {
        records.push_back(makeRecord(level, "svc", "message"));
    }
    return records;
}

loglens::Filter errorsOnly() {
    loglens::ParseError error;
    std::optional<loglens::Filter> filter = loglens::Filter::parse("level >= error", error);
    Q_ASSERT(filter.has_value());
    return *filter;
}

} // namespace

class TestLogModel : public QObject {
    Q_OBJECT

private slots:
    void appendWithoutFilterInsertsAtTheEnd();
    void appendWithFilterInsertsOnlyMatches();
    void appendThatMatchesNothingEmitsNoInsert();
    void resetClearsEverything();
};

// QAbstractItemModelTester asserts the whole QAbstractItemModel contract on
// every signal: begin/end pairing, row counts that agree with the ranges
// announced, index validity. It is the reason this test cannot live in core —
// there is no pure function to extract, and the class does not link without moc.
void TestLogModel::appendWithoutFilterInsertsAtTheEnd() {
    LogModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

    model.setRecords(batch({Level::Info, Level::Warn}));
    QCOMPARE(model.rowCount(), 2);

    QSignalSpy spy(&model, &QAbstractItemModel::rowsInserted);
    model.appendRecords(batch({Level::Error}));

    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(spy.count(), 1);
    // Appends land at the end, so the inserted rows are always one contiguous
    // range — first == old row count, last == new row count - 1.
    QCOMPARE(spy.at(0).at(1).toInt(), 2);
    QCOMPARE(spy.at(0).at(2).toInt(), 2);
}

void TestLogModel::appendWithFilterInsertsOnlyMatches() {
    LogModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

    model.setRecords(batch({Level::Info, Level::Error}));
    const loglens::Filter filter = errorsOnly();
    model.setFilter(&filter);
    QCOMPARE(model.rowCount(), 1);

    QSignalSpy spy(&model, &QAbstractItemModel::rowsInserted);
    model.appendRecords(batch({Level::Debug, Level::Fatal, Level::Info}));

    // Only Fatal passes `level >= error`, so one row appears even though three
    // records arrived. The model still holds all five.
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.totalCount(), 5);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(1).toInt(), 1);
    QCOMPARE(spy.at(0).at(2).toInt(), 1);
}

void TestLogModel::appendThatMatchesNothingEmitsNoInsert() {
    LogModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

    const loglens::Filter filter = errorsOnly();
    model.setFilter(&filter);

    QSignalSpy spy(&model, &QAbstractItemModel::rowsInserted);
    model.appendRecords(batch({Level::Info, Level::Debug}));

    // beginInsertRows with an empty range is a contract violation, so the model
    // must not announce an insert it is not making.
    QCOMPARE(spy.count(), 0);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.totalCount(), 2);
}

void TestLogModel::resetClearsEverything() {
    LogModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

    model.setRecords(batch({Level::Info, Level::Error}));
    QSignalSpy spy(&model, &QAbstractItemModel::modelReset);

    model.resetRecords();

    // Rotation and truncation drop the old content entirely; a reset is the
    // only honest signal for that.
    QCOMPARE(spy.count(), 1);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.totalCount(), 0);
}

QTEST_MAIN(TestLogModel)
#include "test_log_model.moc"
```

`loglens/CMakeLists.txt` 의 `enable_testing()` 아래에 타깃을 더한다.

```cmake
# The Qt model test links the widget library and Qt Test. Before ici 0.6.0 this
# could not be verified at all: moc never ran, so LogModel's vtable was
# unresolved and the test did not link.
add_executable(test_log_model tests/test_log_model.cpp)
target_link_libraries(test_log_model PRIVATE loglens_gui Qt6::Test)
target_include_directories(test_log_model PRIVATE tests)
add_test(NAME test_log_model COMMAND test_log_model WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd loglens
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Debug
cmake --build build/manual --parallel
```
Expected: 컴파일 실패 — `LogModel` 에 `appendRecords` 와 `resetRecords` 가 없다

- [ ] **Step 3: Extend LogModel**

`loglens/src/gui/log_model.hpp` 의 public 구역에 더한다.

```cpp
    // Appends records that arrived after the last poll. Rows that pass the
    // current filter are announced as one contiguous insert at the end, which
    // is always correct because appends never land in the middle.
    void appendRecords(const std::vector<loglens::LogRecord>& records);

    // Drops everything. Used when the source file is truncated or rotated,
    // where the retained rows no longer correspond to anything on disk.
    void resetRecords();
```

private 구역의 `rebuildVisible` 선언 위에 필터 보관을 더한다. `loglens::Filter` 는 `shared_ptr<const Node>` 하나를 들고 있어 값 복사가 싸고 안전하다 — 포인터를 보관하면 `MainWindow` 의 `std::optional<Filter>` 수명에 묶인다.

```cpp
    std::optional<loglens::Filter> filter_;
```

헤더에 `#include <optional>` 을 더한다.

`loglens/src/gui/log_model.cpp` 를 고친다.

```cpp
void LogModel::setFilter(const loglens::Filter* filter) {
    beginResetModel();
    // Copied, not pointed at: appendRecords needs the predicate later, and the
    // caller's optional<Filter> may be reassigned before then.
    filter_ = filter == nullptr ? std::nullopt : std::optional<loglens::Filter>(*filter);
    rebuildVisible(filter);
    endResetModel();
}

void LogModel::appendRecords(const std::vector<loglens::LogRecord>& records) {
    if (records.empty()) {
        return;
    }

    const int first = static_cast<int>(visible_.size());
    std::vector<int> arriving;
    arriving.reserve(records.size());
    for (const loglens::LogRecord& record : records) {
        const int index = static_cast<int>(records_.size() + arriving.size());
        if (!filter_ || filter_->matches(record)) {
            arriving.push_back(index);
        }
    }

    // beginInsertRows with an empty range violates the model contract, so a
    // batch where nothing passes the filter must stay silent about rows while
    // still retaining the records.
    if (arriving.empty()) {
        records_.insert(records_.end(), records.begin(), records.end());
        return;
    }

    beginInsertRows(QModelIndex(), first, first + static_cast<int>(arriving.size()) - 1);
    records_.insert(records_.end(), records.begin(), records.end());
    visible_.insert(visible_.end(), arriving.begin(), arriving.end());
    endInsertRows();
}

void LogModel::resetRecords() {
    beginResetModel();
    records_.clear();
    visible_.clear();
    endResetModel();
}
```

`setRecords` 도 보관된 필터를 지우도록 고친다. 그러지 않으면 새 파일을 열었을 때 이전 필터가 `appendRecords` 에만 남아 `rebuildVisible(nullptr)` 과 어긋난다.

```cpp
void LogModel::setRecords(std::vector<loglens::LogRecord> records) {
    beginResetModel();
    records_ = std::move(records);
    filter_.reset();
    rebuildVisible(nullptr);
    endResetModel();
}
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cd loglens
cmake --build build/manual --parallel
ctest --test-dir build/manual --output-on-failure -R test_log_model
```
Expected: PASS. `QAbstractItemModelTester` 가 위반을 잡으면 즉시 중단된다.

- [ ] **Step 5: Verify through ici — this is the measurement**

```bash
cd loglens
rm -rf build/ici-cmake
../../ici/dist/ici.pyz verify
```
Expected: `Suite: PASS`, 테스트 8건. **`Q_OBJECT` 클래스의 단위 테스트가 ici 안에서 통과한 첫 사례다.**

- [ ] **Step 6: Commit**

```bash
git add loglens/src/gui/log_model.hpp loglens/src/gui/log_model.cpp \
        loglens/tests/test_log_model.cpp loglens/CMakeLists.txt
git commit -m "feat(loglens): append records incrementally, and test the model contract"
```

---

### Task 3: 라이브 팔로우

**Files:**
- Modify: `loglens/src/gui/main_window.hpp`
- Modify: `loglens/src/gui/main_window.cpp`

**Interfaces:**
- Consumes: `LogModel::appendRecords`, `LogModel::resetRecords` (Task 2)
- Produces: 없음 (GUI 동작)

`loglens` 인데 `tail -f` 가 없다. `main_window.cpp:93-104` 의 `openPath` 가 `FileTailer` 를 **지역 변수로** 만들어 한 번 `poll()` 하고 버린다.

- [ ] **Step 1: Keep the tailer alive and poll it on a timer**

`main_window.hpp` 를 고친다. `FileTailer` 는 복사 불가하지 않지만 상태(offset, restarts)를 들고 있어야 하므로 멤버로 보관한다.

```cpp
#include <memory>
#include "loglens/log_source.hpp"

class QCheckBox;
class QTimer;
```

멤버와 슬롯을 더한다.

```cpp
private slots:
    void chooseFile();
    void applyFilter();
    void pollSource();
    void setFollowing(bool following);

private:
    std::unique_ptr<loglens::FileTailer> tailer_;
    QTimer* pollTimer_ = nullptr;
    QCheckBox* followBox_ = nullptr;
    bool autoScroll_ = true;
```

- [ ] **Step 2: Rewrite openPath and add the polling slot**

`main_window.cpp`

```cpp
void MainWindow::openPath(const QString& path) {
    // Owned rather than local: the tailer carries the read offset and the
    // restart count, which is the whole state that makes following work.
    tailer_ = std::make_unique<loglens::FileTailer>(path.toStdString());
    std::vector<std::string> lines;
    std::string error;
    if (!tailer_->poll(lines, error)) {
        // Report it rather than showing an empty window with no explanation.
        status_->setText(tr("Cannot read: %1").arg(QString::fromStdString(error)));
        tailer_.reset();
        return;
    }
    model_->setRecords(parseLines(lines));
    applyFilter();
    setWindowTitle(tr("loglens — %1").arg(path));
    if (followBox_->isChecked()) {
        pollTimer_->start();
    }
}

void MainWindow::pollSource() {
    if (!tailer_) {
        return;
    }
    const std::size_t restartsBefore = tailer_->restarts();
    std::vector<std::string> lines;
    std::string error;
    if (!tailer_->poll(lines, error)) {
        status_->setText(tr("Follow stopped: %1").arg(QString::fromStdString(error)));
        pollTimer_->stop();
        followBox_->setChecked(false);
        return;
    }

    // A truncated or replaced file makes every retained row stale, so the model
    // starts over rather than appending new lines under old ones.
    if (tailer_->restarts() != restartsBefore) {
        model_->resetRecords();
    }
    if (lines.empty()) {
        return;
    }
    model_->appendRecords(parseLines(lines));
    refreshTimeline();
    updateStatus(QString());
    if (autoScroll_) {
        table_->scrollToBottom();
    }
}

void MainWindow::setFollowing(bool following) {
    if (following && tailer_) {
        pollTimer_->start();
        return;
    }
    pollTimer_->stop();
}
```

생성자에 타이머, 체크박스, 자동 스크롤 판정을 더한다.

```cpp
    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(500);
    connect(pollTimer_, &QTimer::timeout, this, &MainWindow::pollSource);

    followBox_ = new QCheckBox(tr("Follow"), this);
    followBox_->setChecked(true);
    connect(followBox_, &QCheckBox::toggled, this, &MainWindow::setFollowing);

    // Following the tail is only wanted while the view is already at the tail.
    // Scrolling up to read something is an explicit request to stay put, and
    // yanking the viewport away from it would make the log unreadable.
    connect(table_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        autoScroll_ = value == table_->verticalScrollBar()->maximum();
    });
```

`followBox_` 를 필터 입력줄이 있는 레이아웃에 넣는다. `<QCheckBox>`, `<QScrollBar>`, `<QTimer>` include 를 더한다.

- [ ] **Step 3: Build and smoke it headlessly**

```bash
cd loglens
cmake --build build/manual --parallel --target loglens-gui
cp tests/data/sample.log /tmp/follow.log
QT_QPA_PLATFORM=offscreen ./build/manual/src/gui/loglens-gui /tmp/follow.log &
sleep 2 && cat tests/data/sample.log >> /tmp/follow.log && sleep 2
kill %1
```
Expected: 크래시 없이 종료. 파일이 자라는 동안 살아 있어야 한다.

- [ ] **Step 4: Verify and commit**

```bash
cd loglens && rm -rf build/ici-cmake && ../../ici/dist/ici.pyz verify
```
Expected: `Suite: PASS`

```bash
git add loglens/src/gui/main_window.hpp loglens/src/gui/main_window.cpp
git commit -m "feat(loglens): follow the file as it grows"
git push -u origin feat/loglens-cmake
gh pr create --title "feat(loglens): CMake build, live follow, and a tested Qt model"
```

---

### Task 4: diskmap 을 루트 qmake 프로젝트로 전환

**Files:**
- Create: `diskmap/diskmap.pro`
- Create: `diskmap/src/src.pro`
- Create: `diskmap/tests/tests.pro`
- Create: `diskmap/src/gui/gui.pro`
- Delete: `diskmap/src/gui/CMakeLists.txt`
- Modify: `diskmap/ici.toml`

**Interfaces:**
- Consumes: 없음
- Produces: qmake 타깃 — `src` 라이브러리, `gui` 라이브러리+앱, `tests` 테스트 앱들

Task 1 과 같은 전환이지만 빌드 시스템이 다르다. **qmake 백엔드가 실물 프로젝트로 검증되는 유일한 자리다** — ici 저장소의 픽스처는 단위 테스트용이지 실측 근거가 아니다.

- [ ] **Step 1: Write the .pro files**

`diskmap/diskmap.pro`

```qmake
TEMPLATE = subdirs
CONFIG += ordered
SUBDIRS = src src/gui tests
src/gui.depends = src
tests.depends = src src/gui
```

`diskmap/src/src.pro`

```qmake
TEMPLATE = lib
CONFIG += staticlib
QT = core
TARGET = diskmap_core
INCLUDEPATH += $$PWD/../include
HEADERS = $$files($$PWD/../include/diskmap/*.hpp)
SOURCES = format.cpp fs_node.cpp fs_source.cpp scanner.cpp treemap.cpp
```

`main.cpp` 는 CLI 진입점이라 라이브러리에 넣지 않는다. CLI 실행 파일이 필요하면 별도 `.pro` 를 두되, 이 계획의 범위는 검증이므로 지금은 만들지 않는다. **ici 의 커버리지 스코프가 진입점을 제외하므로 이것이 수치에 영향을 주지 않는다.**

`diskmap/src/gui/gui.pro`

```qmake
TEMPLATE = lib
CONFIG += staticlib
QT = core gui widgets
TARGET = diskmap_gui
INCLUDEPATH += $$PWD/../../include $$PWD
HEADERS = main_window.hpp treemap_widget.hpp
SOURCES = main_window.cpp treemap_widget.cpp
```

GUI 실행 파일은 별도 `.pro` 로 나눈다 — 라이브러리와 실행 파일을 한 `.pro` 에 담을 수 없다. `diskmap/src/gui/app.pro` 를 만들고 `SUBDIRS` 에 더한다.

```qmake
TEMPLATE = app
QT = core gui widgets
TARGET = diskmap-gui
INCLUDEPATH += $$PWD/../../include $$PWD
SOURCES = gui_main.cpp
LIBS += -L$$OUT_PWD -ldiskmap_gui -L$$OUT_PWD/../ -ldiskmap_core
```

> **여기가 이 태스크에서 가장 불확실한 부분이다.** qmake 의 `SUBDIRS` 안에서 상대 `OUT_PWD` 로 라이브러리를 찾는 경로는 shadow 빌드에서 어긋나기 쉽다. 링크가 실패하면 `LIBS` 를 `$$OUT_PWD/../src` 기준으로 다시 잡거나, `src/gui` 를 하나의 `app` 타깃으로 합치고 테스트가 소스를 직접 컴파일하도록 바꾼다. **실제로 돌려 보고 정한다** — 여기서 추측으로 더 적는 것은 이 저장소가 피하려는 방식이다.

`diskmap/tests/tests.pro`

```qmake
TEMPLATE = subdirs
SUBDIRS = core_tests widget_tests
```

핵심 테스트 5개는 각자 `main()` 을 갖는 평범한 앱이므로 하나의 `.pro` 로 묶을 수 없다. `diskmap/tests/core_tests.pro` 를 만들어 파일마다 타깃을 만드는 대신, **각 테스트를 자체 `.pro` 로 두는 것이 qmake 의 관용이다.** 다섯 개를 만든다(`test_format.pro` 등), 각각:

```qmake
TEMPLATE = app
CONFIG += testcase
CONFIG -= app_bundle
QT = core
TARGET = test_format
INCLUDEPATH += $$PWD/../include $$PWD
SOURCES = test_format.cpp
LIBS += -L$$OUT_PWD/../src -ldiskmap_core
```

`tests.pro` 는 그 다섯을 `SUBDIRS` 로 나열한다.

- [ ] **Step 2: Remove the CMake GUI build**

```bash
git rm diskmap/src/gui/CMakeLists.txt
```

- [ ] **Step 3: Drop the obsolete setting**

`diskmap/ici.toml` 에서 `cpp_external_build_dirs` 를 지운다. 주석은 Task 1 Step 3 과 같은 내용으로 바꾼다.

- [ ] **Step 4: Build and test through qmake directly**

```bash
cd diskmap
mkdir -p build/manual && cd build/manual
qmake6 ../../diskmap.pro
make -j
make check TESTARGS=-xunitxml
```
Expected: 테스트 5개 전부 통과

- [ ] **Step 5: Verify through ici**

```bash
cd diskmap && rm -rf build/ici-qmake && ../../ici/dist/ici.pyz verify
```
Expected: `Suite: PASS`, 백엔드 증거에 `diskmap.pro` 가 남는다

- [ ] **Step 6: Commit**

```bash
git checkout -b feat/diskmap-qmake
git add diskmap/ && git commit -m "build(diskmap): drive the build from a root qmake project"
```

---

### Task 5: TreemapWidget 시그널 테스트

**Files:**
- Create: `diskmap/tests/test_treemap_widget.cpp`
- Create: `diskmap/tests/test_treemap_widget.pro`
- Modify: `diskmap/tests/tests.pro`

**Interfaces:**
- Consumes: Task 4 의 `diskmap_gui` 라이브러리, `TreemapWidget::nodeActivated`, `TreemapWidget::hoveredNodeChanged`
- Produces: 없음

`diskmap` 에는 모델이 없고 `TreemapWidget` 은 커스텀 페인팅 위젯이다. `QAbstractItemModelTester` 는 쓸 수 없지만 **`QSignalSpy` 가 같은 역할을 한다** — Qt Test 링크와 moc 를 동일하게 요구한다.

- [ ] **Step 1: Write the failing test**

`diskmap/tests/test_treemap_widget.cpp`

```cpp
#include <QSignalSpy>
#include <QtTest>

#include "diskmap/fs_node.hpp"
#include "fake_fs.hpp"
#include "treemap_widget.hpp"

class TestTreemapWidget : public QObject {
    Q_OBJECT

private slots:
    void setRootWithNullptrClearsSelection();
    void clickInsideATileActivatesItsNode();
};

// A Q_OBJECT widget links only when moc has run. Before ici 0.6.0 this test
// could not be built by the gate at all.
void TestTreemapWidget::setRootWithNullptrClearsSelection() {
    TreemapWidget widget;
    widget.resize(400, 300);
    widget.setRoot(nullptr);
    QCOMPARE(widget.currentNode(), nullptr);
}

void TestTreemapWidget::clickInsideATileActivatesItsNode() {
    // makeDirNode / makeFileNode are the existing builders in tests/fake_fs.hpp.
    diskmap::FsNode root = makeDirNode("root", {
        makeFileNode("big.bin", 900),
        makeFileNode("small.txt", 100),
    });
    TreemapWidget widget;
    widget.resize(400, 300);
    widget.setRoot(&root);

    QSignalSpy spy(&widget, &TreemapWidget::nodeActivated);
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));

    // The widget owns the hit test that maps a point onto a squarified tile.
    // Nothing about that is extractable into the Qt-free core: the geometry
    // comes from the widget's own size and its paint-time layout.
    QCOMPARE(spy.count(), 1);
}

QTEST_MAIN(TestTreemapWidget)
#include "test_treemap_widget.moc"
```

`diskmap/tests/test_treemap_widget.pro`

```qmake
TEMPLATE = app
CONFIG += testcase
CONFIG -= app_bundle
QT = core gui widgets testlib
TARGET = test_treemap_widget
INCLUDEPATH += $$PWD/../include $$PWD $$PWD/../src/gui
SOURCES = test_treemap_widget.cpp
LIBS += -L$$OUT_PWD/../src/gui -ldiskmap_gui -L$$OUT_PWD/../src -ldiskmap_core
```

`tests.pro` 의 `SUBDIRS` 에 더한다.

- [ ] **Step 2: Run to verify it fails, then passes**

```bash
cd diskmap/build/manual && qmake6 ../../diskmap.pro && make -j
QT_QPA_PLATFORM=offscreen make check TESTARGS=-xunitxml
```
Expected: 처음에는 링크 실패(`diskmap_gui` 를 아직 찾지 못하거나 moc 미실행). 고친 뒤 전부 통과

위젯 테스트는 오프스크린 플랫폼이 필요하다. CI 에서도 같은 환경 변수를 준다.

- [ ] **Step 3: Verify and commit**

```bash
cd diskmap && rm -rf build/ici-qmake && ../../ici/dist/ici.pyz verify
git add diskmap/tests/ && git commit -m "test(diskmap): assert the treemap widget's activation signal"
git push -u origin feat/diskmap-qmake
gh pr create --title "feat(diskmap): qmake build and a tested Qt widget"
```

---

### Task 6: CI

**Files:**
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: Task 1–5
- Produces: 없음

- [ ] **Step 1: Raise the pinned ici version**

`ICI_VERSION: v0.5.5` 를 `v0.6.0` 으로 바꾼다. 체크섬 검증 로직은 그대로다.

- [ ] **Step 2: Give the verify legs the build toolchain**

`verify` 잡의 체크아웃 뒤에 스텝을 더한다. **`qmake6` 는 `qt6-base-dev` 와 별개 패키지다.** cmake 는 러너에 기본 제공된다.

```yaml
      # Both projects are now driven by their own build systems, so the verify
      # leg needs them: loglens configures with cmake, diskmap with qmake6.
      # Qt Test is what the Q_OBJECT unit tests link against.
      - name: Install the Qt6 build toolchain
        run: |
          sudo apt-get update
          sudo apt-get install -y --no-install-recommends qt6-base-dev qmake6 pkg-config
```

`verify` 스텝에 `QT_QPA_PLATFORM: offscreen` 을 준다. `diskmap` 의 위젯 테스트가 디스플레이를 요구한다.

- [ ] **Step 3: Fix the GUI build job for both new layouts**

`gui-build` 잡의 빌드 명령이 프로젝트마다 달라진다. 매트릭스에 빌드 명령을 넣는다.

```yaml
        include:
          - project: diskmap
            smoke_arg: src
            build: |
              mkdir -p build/gui && cd build/gui
              qmake6 ../../diskmap.pro && make -j
            binary: build/gui/src/gui/diskmap-gui
          - project: loglens
            smoke_arg: tests/data/sample.log
            build: |
              cmake -S . -B build/gui -DCMAKE_BUILD_TYPE=Release
              cmake --build build/gui --parallel --target loglens-gui
            binary: build/gui/src/gui/loglens-gui
```

`Install Qt6` 스텝에 `qmake6` 를 더한다.

그리고 이 잡의 근거 주석을 고친다. 현재 문구는 **"The Qt shell is outside ici's scope by design — the core is what gets verified"** 인데 더 이상 사실이 아니다. 어댑터 전환으로 GUI 는 빌드되고 일부는 테스트된다. 잡 자체는 여전히 값이 있다 — 실제 바이너리를 헤드리스로 띄워 링크는 되지만 시작하자마자 죽는 GUI 를 잡는다.

```yaml
  # ici now builds and unit-tests the widget libraries, but it does not launch
  # the application. This job does: a GUI that links and then dies on startup
  # still fails the gate here.
```

- [ ] **Step 4: Verify the workflow parses and commit**

```bash
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/ci.yml')); print('ok')"
git add .github/workflows/ci.yml
git commit -m "ci: pin ici 0.6.0 and give both legs their build systems"
```

---

### Task 7: 문서

**Files:**
- Modify: `README.md`
- Modify: `ICI-GAPS.md`
- Modify: `ROADMAP.md`

**Interfaces:**
- Consumes: Task 1–6 의 실측 결과
- Produces: 없음

- [ ] **Step 1: Rewrite the structure rules in README**

"공통 구조 규칙" 절 전체가 **ici 가 moc 를 못 돌린다는 전제 위에 세워진 우회**였다. 그 전제가 사라졌으므로 다시 쓴다.

- `cpp_external_build_dirs` 설명을 지운다
- "GUI 도 검증 대상이다" 절을 고친다 — 이제 GUI 는 분석뿐 아니라 **빌드와 단위 테스트의 대상**이다
- 프로젝트마다 빌드 시스템이 다르다는 것을 적는다: `loglens` 는 CMake, `diskmap` 은 qmake. **그것이 의도라는 것과 이유**(ici 의 두 어댑터를 각각 실물로 검증한다)를 적는다
- 검증·GUI 빌드 명령을 새 배치에 맞게 고친다
- 필요한 ici 버전을 `0.5.2 이상` 에서 `0.6.0 이상` 으로 올린다

- [ ] **Step 2: Close A-3 and mark A-2 partial in ICI-GAPS**

"남은 것" 표에서 A-3 을 빼고 "수정된 것" 표에 넣는다.

```markdown
| A-3 | 테스트 컴파일이 plain g++ 고정 (moc·gtest 없음) | ✅ #76 / v0.6.0 |
```

A-2 는 **부분 수정**으로 남긴다. CMake·qmake 는 더 이상 거부되지 않지만 손으로 쓴 `Makefile` 만 있는 프로젝트는 여전히 어댑터가 없다. 그 사실을 지우면 남은 거부 경로가 문서에서 사라진다.

A-3 항목 본문에 **실측으로 드러난 두 가지**를 남긴다. 이것이 이 저장소가 존재하는 이유다.

- **두 어댑터의 테스트 카운트 단위가 다르다.** CTest 는 바이너리를, QtTest 는 테스트 함수를 센다. TEM 의 통과/전체에 직접 들어가므로 경로가 다른 프로젝트끼리 TEM 을 비교할 수 없다
- **진입점이 커버리지 분모에 들어오면 전환만으로 커버리지가 떨어진다.** ici `viewer` 에서 branch 71.2%, function 84.5% 로 임계값 아래로 내려갔고, 코드는 하나도 바뀌지 않았다. ici 0.6.0 은 `main()` 을 정의한 번역 단위를 커버리지 스코프에서 제외해 이를 막는다

- [ ] **Step 3: Update ROADMAP**

1단계(loglens 라이브 팔로우)와 2단계 일부가 이 계획으로 처리됐다. 실제로 한 것과 남은 것을 정리한다.

- 1단계 완료. **완료 기준이 "ici 가 어디서 막히는지 기록됐다" 였는데 실제로는 "ici 를 고쳐서 통과시켰다" 가 됐다.** 그 차이와 이유를 적는다
- 2단계(diskmap 정리 작업대)는 **여전히 남아 있다.** 이 계획은 diskmap 을 qmake 로 옮기고 기존 위젯에 시그널 테스트를 붙였을 뿐, 정리 작업대 기능은 만들지 않았다
- 3단계(diff/merge 도구)의 근거가 **좁아졌다.** A-2·A-3 실측이라는 원래 목적은 이미 달성됐고, 남은 것은 `.qrc`/`.ui` 생성 단계(`AUTOUIC`/`AUTORCC`)뿐이다. 그것만을 위해 새 프로젝트를 만들 가치가 있는지 다시 판단한다

- [ ] **Step 4: Commit**

```bash
git add README.md ICI-GAPS.md ROADMAP.md
git commit -m "docs: record what the build adapters changed, and what is left"
```

---

## 자체 검토 결과

**스펙 커버리지**

| 스펙 절 | 태스크 |
|---|---|
| §4.1 loglens → CMake | Task 1, 2, 3 |
| §4.2 diskmap → qmake | Task 4, 5 |
| §6 CI | Task 6 |
| §3.7 설정 계약 변화의 사용자 쪽 반영 | Task 1 Step 3, Task 4 Step 3, Task 7 |

**불확실한 곳 하나를 표시해 두었다.** Task 4 Step 1 의 qmake `SUBDIRS` 간 라이브러리 링크 경로다. shadow 빌드에서 `OUT_PWD` 상대 경로가 어긋나기 쉬운데, 여기서 더 정밀하게 적는 것은 실측이 아니라 추측이 된다. 실제로 돌려 보고 정하며, 달라지면 이 계획을 고친다.

**의도적으로 하지 않는 것.** `diskmap` 의 CLI 실행 파일용 `.pro` 를 만들지 않는다. `src/main.cpp` 는 ici 의 커버리지 스코프에서 제외되므로 수치에 영향이 없고, 이 계획의 목적은 검증 경로 전환이지 배포물 유지가 아니다. CLI 바이너리가 실제로 필요해지면 그때 별도 작업으로 만든다.

---

## 실제로 어떻게 됐나 (2026-08-30, 완료 후 기록)

계획은 대체로 맞았고, 세 곳이 틀렸다. 남겨 두는 이유는 **어디서 틀렸는지가 다음 계획을 쓸 때
쓸모 있기 때문**이다.

### 맞은 것

- **Task 4 Step 1 을 "가장 불확실" 로 표시한 판단은 과했다.** qmake `SUBDIRS` 간 상대
  `OUT_PWD` 링크는 한 번에 통과했다. 대신 진짜 문제는 다른 데 있었다 — 아래를 본다.
- loglens 의 `beginInsertRows` 범위 계산이 "append 는 항상 끝에 붙으므로 단일 연속 구간"
  이라는 예상은 그대로 맞았다.
- diskmap 의 `QSignalSpy` 가 `QAbstractItemModelTester` 의 대역이 된다는 판단도 맞았다.

### 틀린 것 1 — `sanitize` 를 건드릴 필요가 없다고 본 것

계획은 ici 쪽 변경이 끝났다고 전제했다. 아니었다. `sanitize` 는 `tests/**/*.cpp` 를 각각
plain g++ 로 컴파일하므로, **Qt 테스트를 `tests/` 에 두는 순간 헤더를 못 찾고 깨진다.**
"Qt 테스트를 `tests/` 안에 두고 통과시킨다" 는 이 계획의 목표가 `sanitize` 전환을 전제하고
있었는데, 그걸 계획 단계에서 못 봤다.

### 틀린 것 2 — 커버리지 임계값을 "필요하면 조정" 정도로 본 것

Task 1 Step 5 는 미달 시 조정하라고만 적었다. 실제로는 **왜 미달인지가 핵심**이었다. GUI 가
분모에 들어오면서 테스트 없는 Qt 셸 165 statements 가 측정에 잡혔고, 그건 코드가 나빠진 게
아니라 보이지 않던 코드가 보이기 시작한 것이다. `loglens` 는 80/90 → 55/80 으로 내렸고
`diskmap` 은 셸이 작아 그대로 통과했다. **같은 조치라도 이유가 다르면 다른 일이다.**

### 틀린 것 3 — 픽스처가 실물을 대신할 수 있다고 암묵적으로 가정한 것

계획은 ici 픽스처가 어댑터를 검증한다고 적었다(§4.4). 실제로는 `diskmap` 을 전환하고 나서야
드러난 결함이 둘 있었다.

- **qmake 는 Qt 링크 테스트를 `target_wrapper.sh` 로 실행한다.** ici 가 그 줄을 못 읽어
  6개 중 5개만 셌다. 픽스처는 Qt 테스트 하나뿐이라 다른 경로로 구제됐다.
- **qmake 는 상대 경로로 컴파일한다.** 그래서 `.gcov` 파일명이 `^#^#^#src#format.cpp` 가
  되고 line 커버리지가 통째로 유실됐다. 픽스처도 같은 조건이었지만 커버리지 **존재 여부**만
  단언하고 값을 보지 않아 통과했다.

두 번째가 특히 뼈아프다 — 픽스처가 그 코드 경로를 실제로 지나갔는데도 단언이 느슨해서
놓쳤다. **픽스처를 만들 때 "지나가는가" 가 아니라 "무엇을 단언하는가" 를 봐야 한다.**

전체 결과와 발견 목록은 [`../../../ICI-GAPS.md`](../../../ICI-GAPS.md) 와
[`../../../ROADMAP.md`](../../../ROADMAP.md) 에 있다.
