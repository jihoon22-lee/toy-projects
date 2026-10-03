#include "buildscope/contract.hpp"
#include "buildscope/impact.hpp"
#include "native_analysis.hpp"
#include "native_include.hpp"
#include "native_relocation.hpp"
#include "native_replay.hpp"
#include "native_snapshot.hpp"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <chrono>
#include <thread>

using namespace buildscope::native;
namespace {
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size())
        throw std::runtime_error("fixture write failed");
}
QJsonObject fixture(const QString &root, bool missing = true) {
    QDir().mkpath(root + "/include");
    write(root + "/main.cpp", missing ? "#include \"include/a.hpp\"\n#include \"missing.hpp\"\n"
                                      : "#include \"include/a.hpp\"\n");
    write(root + "/include/a.hpp", "#include \"b.hpp\"\n");
    write(root + "/include/b.hpp", "#define B 1\n");
    QJsonArray db{QJsonObject{{"directory", root},
                              {"file", "main.cpp"},
                              {"arguments", QJsonArray{QStringLiteral(BUILDSCOPE_TEST_COMPILER),
                                                       "-O2", "-c", "main.cpp"}}}};
    write(root + "/compile_commands.json", QJsonDocument(db).toJson());
    return loadCompilationDatabase(root + "/compile_commands.json", root);
}
QJsonObject analysis(const QJsonObject &snapshot) {
    return snapshot.value("entries")
        .toArray()
        .first()
        .toObject()
        .value("include_analysis")
        .toObject();
}
} // namespace
class AnalysisWorkflowTest : public QObject {
    Q_OBJECT
  private slots:
    void partialCompilerKeepsSeparateFallback();
    void budgetsCancellationAndMutation();
    void traceTimeoutRetainsPrefixAndKillsChild();
    void reverseImpactAndRelocation();
    void v4RejectsInvalidProvenance();
    void cliRoundTrip();
    void ambiguousDirectiveLineStaysUnknown();
    void relocatedSnapshotKeepsDatabaseRelativeDirectory();
};
void AnalysisWorkflowTest::partialCompilerKeepsSeparateFallback() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto snapshot = fixture(dir.path());
    AnalysisControl control;
    annotateSnapshotControlled(snapshot, dir.path(), "compiler", control);
    auto result = analysis(snapshot);
    QCOMPARE(result.value("evidence").toString(), QString("compiler-measured"));
    QVERIFY(!result.value("complete").toBool());
    QVERIFY(result.value("fallback").isObject());
    QVERIFY(result.value("edges").toArray().size() >= 2);
    QCOMPARE(result.value("fallback").toObject().value("evidence").toString(),
             QString("estimated"));
    const auto parsed = buildscope::parseSnapshot(QJsonDocument(snapshot));
    QVERIFY(parsed.entries[0].includeAnalysis.fallback);
    QVERIFY(!parsed.entries[0].includeAnalysis.complete);
    const auto impact = buildscope::includeImpact(parsed, "include/b.hpp");
    QVERIFY(impact.value("partial").toBool());
    const auto units = impact.value("translation_units").toArray();
    QCOMPARE(units.size(), 2);
    QCOMPARE(units[0].toObject().value("evidence").toString(), QString("compiler-measured"));
    QCOMPARE(units[1].toObject().value("evidence").toString(), QString("estimated"));
    QCOMPARE(units[0].toObject().value("chain").toArray(),
             QJsonArray({"main.cpp", "include/a.hpp", "include/b.hpp"}));
    // The unchanged strict v3 reader accepts explicitly projected legacy output.
    const auto old = snapshotForSchema(snapshot, "v3");
    QCOMPARE(buildscope::parseSnapshot(QJsonDocument(old)).schemaVersion,
             QString("buildscope.snapshot/v3"));
    QVERIFY(!analysis(old).contains("fallback"));
}
void AnalysisWorkflowTest::budgetsCancellationAndMutation() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto original = fixture(dir.path(), false);
    AnalysisLimits limits;
    limits.unitSourceBytes = 8;
    AnalysisControl byteControl(limits);
    auto snapshot = original;
    annotateSnapshotControlled(snapshot, dir.path(), "estimate", byteControl);
    QVERIFY(!analysis(snapshot).value("complete").toBool());
    QVERIFY(analysis(snapshot).value("stop_reason").toString().contains("byte budget"));
    QCOMPARE(byteControl.sourceBytes, qint64(8));
    std::atomic_bool cancel = true;
    AnalysisControl cancelled({}, &cancel);
    snapshot = original;
    annotateSnapshotControlled(snapshot, dir.path(), "compiler", cancelled);
    QVERIFY(snapshot.value("analysis_run").toObject().value("cancelled").toBool());
    QCOMPARE(cancelled.units, 0);
    QVERIFY_EXCEPTION_THROWN(
        loadCompilationDatabase(dir.path() + "/compile_commands.json", dir.path(), {}, &cancel),
        std::exception);
    AnalysisControl mutation;
    bool changed = false;
    mutation.sourceReadObserver = [&](const QString &path, qint64) {
        if (!changed) {
            changed = true;
            QFile f(path);
            if (f.open(QIODevice::Append))
                f.write(QByteArray(70000, 'x'));
        }
    };
    snapshot = original;
    annotateSnapshotControlled(snapshot, dir.path(), "estimate", mutation);
    QVERIFY(changed);
    QVERIFY(!analysis(snapshot).value("complete").toBool());
    QVERIFY(analysis(snapshot).value("stop_reason").toString().contains("changed"));
    // Cancellation while parsing an already loaded document is independently honored.
    QVERIFY_EXCEPTION_THROWN(buildscope::parseSnapshot(QJsonDocument(original), &cancel),
                             buildscope::ContractError);
}
void AnalysisWorkflowTest::traceTimeoutRetainsPrefixAndKillsChild() {
    AnalysisLimits limits;
    limits.unitMilliseconds = 60;
    limits.totalMilliseconds = 500;
    AnalysisControl control(limits);
    control.beginUnit();
    AnalysisScope scope(&control);
    QElapsedTimer timer;
    timer.start();
    auto result = runTraceControlled(
        {"/bin/sh", "-c", "printf '. /tmp/observed-header\\n' >&2; sleep 5"}, QDir::tempPath());
    QVERIFY(timer.elapsed() < 1500);
    QVERIFY(!result.complete);
    QVERIFY(result.stopReason.contains("time budget"));
    QVERIFY(result.text.contains("/tmp/observed-header"));
    AnalysisLimits cap;
    cap.traceBytes = 32;
    AnalysisControl output(cap);
    output.beginUnit();
    AnalysisScope outputScope(&output);
    const auto capped =
        runTraceControlled({"/bin/sh", "-c",
                            "printf '. /tmp/a\\n'; i=0; while [ $i -lt 100 ]; do printf "
                            "'diagnostic line\\n' >&2; i=$((i+1)); done"},
                           QDir::tempPath());
    QVERIFY(!capped.complete);
    QVERIFY(capped.stopReason.contains("output budget"));
    QVERIFY(capped.text.toUtf8().size() <= 32);
    std::atomic_bool flag = false;
    AnalysisControl interrupted({}, &flag);
    interrupted.beginUnit();
    AnalysisScope interruptScope(&interrupted);
    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        flag = true;
    });
    const auto stopped = runTraceControlled({"/bin/sh", "-c", "sleep 5"}, QDir::tempPath());
    stopper.join();
    QVERIFY(stopped.stopReason.contains("cancelled"));
}
void AnalysisWorkflowTest::reverseImpactAndRelocation() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto snapshot = fixture(dir.path(), false);
    auto mappings = parseRootMappings({"/old/project=" + dir.path()});
    QCOMPARE(relocatePath("/old/project/include/a.hpp", mappings),
             dir.path() + "/include/a.hpp");
    QCOMPARE(relocatePath("/old/project-other/a.hpp", mappings),
             QString("/old/project-other/a.hpp"));
    auto raw =
        QJsonObject{{"directory", "/old/project"},
                    {"file", "/old/project/main.cpp"},
                    {"arguments", QJsonArray{QStringLiteral(BUILDSCOPE_TEST_COMPILER),
                                             "-DVALUE=/old/project", "-I/old/project/include",
                                             "-c", "/old/project/main.cpp"}}};
    auto mapped = relocateInvocation(raw, mappings);
    auto argv = mapped.value("arguments").toArray();
    QCOMPARE(argv[1].toString(), QString("-DVALUE=/old/project"));
    QCOMPARE(argv[2].toString(), "-I" + dir.path() + "/include");
    write(dir.path() + "/old.json", QJsonDocument(QJsonArray{raw}).toJson());
    auto relocated = loadCompilationDatabase(dir.path() + "/old.json", dir.path(), mappings);
    AnalysisControl control;
    annotateSnapshotControlled(relocated, dir.path(), "estimate", control);
    const auto parsed = buildscope::parseSnapshot(QJsonDocument(relocated));
    const auto impact = buildscope::includeImpact(parsed, dir.path() + "/include/b.hpp");
    QCOMPARE(impact.value("translation_units").toArray().size(), 1);
    QCOMPARE(impact.value("direct_parents").toArray()[0].toObject().value("parent").toString(),
             QString("include/a.hpp"));
    QVERIFY(buildscope::includeImpact(parsed, "include/b.hpp", nullptr, 1)
                .value("partial")
                .toBool());
    QVERIFY_EXCEPTION_THROWN(parseRootMappings({"relative=/tmp"}), std::exception);
}
void AnalysisWorkflowTest::v4RejectsInvalidProvenance() {
    QTemporaryDir dir;
    auto snapshot = fixture(dir.path(), false);
    AnalysisControl control;
    annotateSnapshotControlled(snapshot, dir.path(), "estimate", control);
    auto entries = snapshot.value("entries").toArray();
    auto entry = entries[0].toObject();
    auto a = entry.value("include_analysis").toObject();
    a.insert("stop_reason", "stopped");
    entry.insert("include_analysis", a);
    entries[0] = entry;
    auto bad = snapshot;
    bad.insert("entries", entries);
    QVERIFY_EXCEPTION_THROWN(buildscope::parseSnapshot(QJsonDocument(bad)),
                             buildscope::ContractError);
    auto run = snapshot.value("analysis_run").toObject();
    auto budgets = run.value("limits").toObject();
    budgets.insert("unit_ms", 0);
    run.insert("limits", budgets);
    bad = snapshot;
    bad.insert("analysis_run", run);
    QVERIFY_EXCEPTION_THROWN(buildscope::parseSnapshot(QJsonDocument(bad)),
                             buildscope::ContractError);
}
void AnalysisWorkflowTest::cliRoundTrip() {
    QTemporaryDir dir;
    fixture(dir.path());
    const auto output = dir.filePath("snapshot.json");
    QProcess cli;
    cli.start(QStringLiteral(BUILDSCOPE_TEST_PRODUCER),
              {dir.filePath("compile_commands.json"), "--project-root", dir.path(),
               "--include-analysis", "compiler", "--output", output});
    QVERIFY(cli.waitForFinished(10000));
    QCOMPARE(cli.exitCode(), 0);
    const auto parsed = buildscope::loadSnapshotFile(output);
    QCOMPARE(parsed.schemaVersion, QString("buildscope.snapshot/v4"));
    QVERIFY(parsed.entries[0].includeAnalysis.fallback);
    cli.start(QStringLiteral(BUILDSCOPE_TEST_PRODUCER),
              {"impact", output, "--header", "include/b.hpp"});
    QVERIFY(cli.waitForFinished(5000));
    QCOMPARE(cli.exitCode(), 0);
    const auto report = QJsonDocument::fromJson(cli.readAllStandardOutput()).object();
    QCOMPARE(report.value("translation_units").toArray().size(), 2);
    cli.start(QStringLiteral(BUILDSCOPE_TEST_PRODUCER),
              {dir.filePath("compile_commands.json"), "--project-root", dir.path(),
               "--include-analysis", "estimate", "--analysis-unit-bytes", "8", "--output",
               output});
    QVERIFY(cli.waitForFinished(5000));
    QCOMPARE(cli.exitCode(), 0);
    QVERIFY(!buildscope::loadSnapshotFile(output).entries[0].includeAnalysis.complete);
    cli.start(QStringLiteral(BUILDSCOPE_TEST_PRODUCER),
              {dir.filePath("compile_commands.json"), "--output",
               dir.filePath("compile_commands.json")});
    QVERIFY(cli.waitForFinished(5000));
    QCOMPARE(cli.exitCode(), 2);
}

void AnalysisWorkflowTest::ambiguousDirectiveLineStaysUnknown() {
    QTemporaryDir dir;
    auto snapshot = fixture(dir.path(), false);
    write(dir.filePath("main.cpp"),
          "#if 0\n#include \"include/a.hpp\"\n#endif\n#include \"include/a.hpp\"\n");
    AnalysisControl control;
    annotateSnapshotControlled(snapshot, dir.path(), "compiler", control);
    const auto edges = analysis(snapshot).value("edges").toArray();
    QVERIFY(!edges.isEmpty());
    QCOMPARE(edges[0].toObject().value("line").toInt(), 0);
    QCOMPARE(edges[0].toObject().value("location_evidence").toString(), QString("unavailable"));
}

void AnalysisWorkflowTest::relocatedSnapshotKeepsDatabaseRelativeDirectory() {
    QTemporaryDir oldRoot, newRoot;
    QDir().mkpath(oldRoot.filePath("build"));
    QDir().mkpath(newRoot.filePath("build"));
    write(oldRoot.filePath("unit.cpp"), "int unit;\n");
    write(newRoot.filePath("unit.cpp"), "int unit;\n");
    QJsonArray db{QJsonObject{{"directory", "."},
                              {"file", "../unit.cpp"},
                              {"arguments", QJsonArray{"c++", "-c", "../unit.cpp"}}}};
    write(oldRoot.filePath("build/compile_commands.json"), QJsonDocument(db).toJson());
    auto snapshot = loadCompilationDatabase(oldRoot.filePath("build/compile_commands.json"),
                                            oldRoot.path());
    const auto mappings = parseRootMappings({oldRoot.path() + '=' + newRoot.path()});
    auto moved = relocateSnapshot(snapshot, newRoot.path(), mappings);
    const auto entry = moved.value("entries").toArray()[0].toObject();
    const auto [cwd, source] = nativeEntryPaths(entry, newRoot.path());
    QCOMPARE(cwd, newRoot.filePath("build"));
    QCOMPARE(source, newRoot.filePath("unit.cpp"));
    AnalysisControl control;
    annotateSnapshotControlled(moved, newRoot.path(), "estimate", control);
    QVERIFY(analysis(moved).value("complete").toBool());
}

QTEST_GUILESS_MAIN(AnalysisWorkflowTest)
#include "test_analysis_workflow.moc"
