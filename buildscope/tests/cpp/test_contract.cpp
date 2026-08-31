#include "buildscope/contract.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class ContractTest final : public QObject {
    Q_OBJECT

private slots:
    void loadsVersionedSnapshot();
    void rejectsMismatchedEntryCount();
    void rejectsInvalidContracts();
    void rejectsOversizedFile();
    void rejectsTooManyEntries();
};

namespace {

void expectContractError(const QByteArray &payload, const QString &message) {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto path = temporary.filePath(QStringLiteral("snapshot.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(payload), payload.size());
    file.close();

    try {
        buildscope::loadSnapshotFile(path);
        QFAIL("expected buildscope::ContractError");
    } catch (const buildscope::ContractError &error) {
        QVERIFY2(QString::fromUtf8(error.what()).contains(message), error.what());
    }
}

}  // namespace

void ContractTest::loadsVersionedSnapshot() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto path = temporary.filePath(QStringLiteral("snapshot.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"json({
      "schema_version":"buildscope.snapshot/v1",
      "producer":{"name":"buildscope","version":"0.1.0"},
      "source":{"path":"compile_commands.json","entry_count":1},
      "entries":[{"file":"src/main.cpp","directory":"build","arguments":["c++","-c","src/main.cpp"],"command":null,"output":null}]
    })json");
    file.close();

    const auto snapshot = buildscope::loadSnapshotFile(path);

    QCOMPARE(snapshot.schemaVersion, QStringLiteral("buildscope.snapshot/v1"));
    QCOMPARE(snapshot.entries.size(), 1);
    QCOMPARE(snapshot.entries.front().file, QStringLiteral("src/main.cpp"));
    QCOMPARE(
        buildscope::invocationText(snapshot.entries.front()),
        QStringLiteral("c++ -c src/main.cpp"));
}

void ContractTest::rejectsMismatchedEntryCount() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto path = temporary.filePath(QStringLiteral("snapshot.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"json({
      "schema_version":"buildscope.snapshot/v1",
      "producer":{"name":"buildscope","version":"0.1.0"},
      "source":{"path":"compile_commands.json","entry_count":2},
      "entries":[]
    })json");
    file.close();

    QVERIFY_EXCEPTION_THROWN(buildscope::loadSnapshotFile(path), buildscope::ContractError);
}

void ContractTest::rejectsInvalidContracts() {
    const QList<QPair<QByteArray, QString>> cases = {
        {"{", QStringLiteral("invalid JSON at byte")},
        {"[]", QStringLiteral("snapshot root must be an object")},
        {R"json({"schema_version":"other"})json",
         QStringLiteral("root.schema_version is unsupported")},
        {R"json({"schema_version":"buildscope.snapshot/v1"})json",
         QStringLiteral("root.producer must be an object")},
        {R"json({"schema_version":"buildscope.snapshot/v1","producer":{}})json",
         QStringLiteral("root.producer.name must be a non-empty string")},
        {R"json({"schema_version":"buildscope.snapshot/v1","producer":{"name":"other","version":"1"}})json",
         QStringLiteral("root.producer.name is unsupported")},
        {R"json({"schema_version":"buildscope.snapshot/v1","producer":{"name":"buildscope"}})json",
         QStringLiteral("root.producer.version must be a non-empty string")},
        {R"json({"schema_version":"buildscope.snapshot/v1","producer":{"name":"buildscope","version":"1"}})json",
         QStringLiteral("root.source must be an object")},
        {R"json({"schema_version":"buildscope.snapshot/v1","producer":{"name":"buildscope","version":"1"},"source":{"path":"db","entry_count":0}})json",
         QStringLiteral("root.entries must be an array")},
        {R"json({"schema_version":"buildscope.snapshot/v1","producer":{"name":"buildscope","version":"1"},"source":{"path":"db","entry_count":1.5},"entries":[null]})json",
         QStringLiteral("root.source.entry_count must match")},
        {R"json({"schema_version":"buildscope.snapshot/v1","producer":{"name":"buildscope","version":"1"},"source":{"path":"db","entry_count":1},"entries":[null]})json",
         QStringLiteral("entries[0] must be an object")},
        {R"json({"schema_version":"buildscope.snapshot/v1","producer":{"name":"buildscope","version":"1"},"source":{"path":"db","entry_count":1},"entries":[{"file":"a.cpp","directory":"build"}]})json",
         QStringLiteral("exactly one of arguments or command")},
        {R"json({"schema_version":"buildscope.snapshot/v1","producer":{"name":"buildscope","version":"1"},"source":{"path":"db","entry_count":1},"entries":[{"file":"a.cpp","directory":"build","arguments":[]}]})json",
         QStringLiteral("arguments must be a non-empty string array")},
        {R"json({"schema_version":"buildscope.snapshot/v1","producer":{"name":"buildscope","version":"1"},"source":{"path":"db","entry_count":1},"entries":[{"file":"a.cpp","directory":"build","arguments":[1]}]})json",
         QStringLiteral("arguments[0] must be a string")},
        {R"json({"schema_version":"buildscope.snapshot/v1","producer":{"name":"buildscope","version":"1"},"source":{"path":"db","entry_count":1},"entries":[{"file":"a.cpp","directory":"build","command":1}]})json",
         QStringLiteral("command must be a non-empty string or null")},
        {R"json({"schema_version":"buildscope.snapshot/v1","producer":{"name":"buildscope","version":"1"},"source":{"path":"db","entry_count":1},"entries":[{"file":"a.cpp","directory":"build","arguments":["c++"],"command":"c++ a.cpp"}]})json",
         QStringLiteral("exactly one of arguments or command")},
        {R"json({"schema_version":"buildscope.snapshot/v1","producer":{"name":"buildscope","version":"1"},"source":{"path":"db","entry_count":1},"entries":[{"file":"a.cpp","directory":"build","arguments":["c++"],"output":1}]})json",
         QStringLiteral("output must be a non-empty string or null")},
        {R"json({"schema_version":"buildscope.snapshot/v1","producer":{"name":"buildscope","version":"1"},"source":{"path":"db","entry_count":1},"entries":[{"file":"","directory":"build","command":"c++"}]})json",
         QStringLiteral("file must be a non-empty string")},
    };

    for (const auto &testCase : cases) {
        expectContractError(testCase.first, testCase.second);
    }
}

void ContractTest::rejectsOversizedFile() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QFile file(temporary.filePath(QStringLiteral("snapshot.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.resize(64LL * 1024LL * 1024LL + 1));
    file.close();

    QVERIFY_EXCEPTION_THROWN(buildscope::loadSnapshotFile(file.fileName()), buildscope::ContractError);
}

void ContractTest::rejectsTooManyEntries() {
    QByteArray entries;
    entries.reserve(500010);
    for (int index = 0; index < 100001; ++index) {
        if (index != 0) {
            entries.append(',');
        }
        entries.append("null");
    }
    const auto payload =
        QByteArray(R"json({"schema_version":"buildscope.snapshot/v1","producer":{"name":"buildscope","version":"1"},"source":{"path":"db","entry_count":100001},"entries":[)json") +
        entries + "]}";

    expectContractError(payload, QStringLiteral("root.entries exceeds 100000 entry limit"));
}

QTEST_APPLESS_MAIN(ContractTest)

#include "test_contract.moc"
