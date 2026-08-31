#include "buildscope/contract.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

class ContractTest final : public QObject {
    Q_OBJECT

private slots:
    void loadsVersionedSnapshot();
    void loadsNormalizedV2Snapshot();
    void acceptsCommandOnlyV2Entry();
    void acceptsEmptyNonCompilerV2Arg();
    void preservesEmptyV1Arguments();
    void rejectsMalformedV2Core();
    void rejectsMismatchedEntryCount();
    void rejectsInvalidContracts();
    void rejectsDuplicateJsonKeys();
    void rejectsFinalSymlink();
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

QByteArray validV2Payload() {
    return R"json({
      "schema_version":"buildscope.snapshot/v2",
      "producer":{"name":"buildscope","version":"0.2.0"},
      "source":{"path":"compile_commands.json","project_root":"/project","entry_count":1},
      "entries":[{
        "file":"src/main.cpp",
        "directory":"build",
        "arguments":["c++","-std=c++20","-DFEATURE=1","-Iinclude","-c","src/main.cpp"],
        "command":"c++ -std=c++20 -DFEATURE=1 -Iinclude -c src/main.cpp",
        "output":null,
        "normalized":{
          "argv":["c++","-std=c++20","-DFEATURE=1","-Iinclude","-c","src/main.cpp"],
          "command_style":"posix",
          "compiler":{"family":"gcc","name":"c++","path":"/usr/bin/c++","wrappers":[]},
          "configuration":"sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
          "defines":[{"action":"define","name":"FEATURE","value":"1"}],
          "directory":{"exists":true,"path":"build","scope":"project","style":"posix"},
          "include_paths":[{"exists":true,"kind":"include","order":0,"path":"include","scope":"project","style":"posix"}],
          "invocation_source":"arguments",
          "language":"c++",
          "output":null,
          "source":{"exists":true,"path":"src/main.cpp","scope":"project","style":"posix"},
          "standard":"c++20",
          "sysroot":null,
          "target":{"build_target":"app","triple":"x86_64-linux-gnu"}
        },
        "state":{"duplicate":false,"entry_index":0,"source_configuration_count":1,"source_status":"present"},
        "diagnostics":[{"code":"notice","message":"A stable diagnostic.","severity":"warning"}]
      }]
    })json";
}

template <typename Mutator>
QByteArray mutateV2Entry(Mutator mutator) {
    auto document = QJsonDocument::fromJson(validV2Payload());
    auto root = document.object();
    auto entries = root.value(QStringLiteral("entries")).toArray();
    auto entry = entries.at(0).toObject();
    mutator(entry);
    entries.replace(0, entry);
    root.insert(QStringLiteral("entries"), entries);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

template <typename Mutator>
QByteArray mutateV2Snapshot(Mutator mutator) {
    auto document = QJsonDocument::fromJson(validV2Payload());
    auto root = document.object();
    mutator(root);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

template <typename Mutator>
QByteArray mutateV2Normalized(Mutator mutator) {
    return mutateV2Entry([&](QJsonObject &entry) {
        auto normalized = entry.value(QStringLiteral("normalized")).toObject();
        mutator(normalized);
        entry.insert(QStringLiteral("normalized"), normalized);
    });
}

template <typename Mutator>
QByteArray mutateV2Compiler(Mutator mutator) {
    return mutateV2Normalized([&](QJsonObject &normalized) {
        auto compiler = normalized.value(QStringLiteral("compiler")).toObject();
        mutator(compiler);
        normalized.insert(QStringLiteral("compiler"), compiler);
    });
}

template <typename Mutator>
QByteArray mutateV2State(Mutator mutator) {
    return mutateV2Entry([&](QJsonObject &entry) {
        auto state = entry.value(QStringLiteral("state")).toObject();
        mutator(state);
        entry.insert(QStringLiteral("state"), state);
    });
}

template <typename Mutator>
QByteArray mutateV2Diagnostics(Mutator mutator) {
    return mutateV2Entry([&](QJsonObject &entry) {
        auto diagnostics = entry.value(QStringLiteral("diagnostics")).toArray();
        auto diagnostic = diagnostics.at(0).toObject();
        mutator(diagnostic);
        diagnostics.replace(0, diagnostic);
        entry.insert(QStringLiteral("diagnostics"), diagnostics);
    });
}

template <typename Mutator>
QByteArray mutateV2Path(Mutator mutator, const QString &field = QStringLiteral("directory")) {
    return mutateV2Normalized([&](QJsonObject &normalized) {
        auto path = normalized.value(field).toObject();
        mutator(path);
        normalized.insert(field, path);
    });
}

template <typename Mutator>
QByteArray mutateV2Include(Mutator mutator) {
    return mutateV2Normalized([&](QJsonObject &normalized) {
        auto includes = normalized.value(QStringLiteral("include_paths")).toArray();
        auto include = includes.at(0).toObject();
        mutator(include);
        includes.replace(0, include);
        normalized.insert(QStringLiteral("include_paths"), includes);
    });
}

template <typename Mutator>
QByteArray mutateV2Define(Mutator mutator) {
    return mutateV2Normalized([&](QJsonObject &normalized) {
        auto defines = normalized.value(QStringLiteral("defines")).toArray();
        auto define = defines.at(0).toObject();
        mutator(define);
        defines.replace(0, define);
        normalized.insert(QStringLiteral("defines"), defines);
    });
}

template <typename Mutator>
QByteArray mutateV2Target(Mutator mutator) {
    return mutateV2Normalized([&](QJsonObject &normalized) {
        auto target = normalized.value(QStringLiteral("target")).toObject();
        mutator(target);
        normalized.insert(QStringLiteral("target"), target);
    });
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

void ContractTest::loadsNormalizedV2Snapshot() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto path = temporary.filePath(QStringLiteral("snapshot.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const auto payload = validV2Payload();
    QCOMPARE(file.write(payload), payload.size());
    file.close();

    const auto snapshot = buildscope::loadSnapshotFile(path);

    QCOMPARE(snapshot.schemaVersion, QStringLiteral("buildscope.snapshot/v2"));
    QCOMPARE(snapshot.projectRoot, QStringLiteral("/project"));
    QCOMPARE(snapshot.entries.size(), 1);
    const auto &entry = snapshot.entries.front();
    QVERIFY(entry.hasNormalized);
    QVERIFY(entry.hasState);
    QCOMPARE(entry.normalized.argv.size(), 6);
    QCOMPARE(entry.normalized.compiler.family, QStringLiteral("gcc"));
    QCOMPARE(entry.normalized.compiler.wrappers.size(), 0);
    QCOMPARE(entry.normalized.invocationSource, QStringLiteral("arguments"));
    QCOMPARE(entry.normalized.configuration,
             QStringLiteral("sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    QCOMPARE(entry.normalized.defines.size(), 1);
    QCOMPARE(entry.normalized.defines.front().name, QStringLiteral("FEATURE"));
    QCOMPARE(entry.normalized.includePaths.size(), 1);
    QCOMPARE(entry.normalized.includePaths.front().order, qsizetype(0));
    QCOMPARE(entry.normalized.language, QStringLiteral("c++"));
    QCOMPARE(entry.normalized.standard, QStringLiteral("c++20"));
    QCOMPARE(entry.normalized.target.buildTarget, QStringLiteral("app"));
    QCOMPARE(entry.normalized.target.triple, QStringLiteral("x86_64-linux-gnu"));
    QCOMPARE(entry.state.sourceConfigurationCount, qsizetype(1));
    QCOMPARE(entry.state.sourceStatus, QStringLiteral("present"));
    QCOMPARE(entry.diagnostics.size(), 1);
    QCOMPARE(entry.diagnostics.front().severity, QStringLiteral("warning"));
    QCOMPARE(buildscope::invocationText(entry),
             QStringLiteral("c++ -std=c++20 -DFEATURE=1 -Iinclude -c src/main.cpp"));
}

void ContractTest::acceptsEmptyNonCompilerV2Arg() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto path = temporary.filePath(QStringLiteral("snapshot.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const auto payload = mutateV2Entry([](QJsonObject &entry) {
        auto arguments = entry.value(QStringLiteral("arguments")).toArray();
        arguments.insert(1, QString());
        entry.insert(QStringLiteral("arguments"), arguments);
        auto normalized = entry.value(QStringLiteral("normalized")).toObject();
        normalized.insert(QStringLiteral("argv"), arguments);
        entry.insert(QStringLiteral("normalized"), normalized);
    });
    QCOMPARE(file.write(payload), payload.size());
    file.close();

    const auto snapshot = buildscope::loadSnapshotFile(path);

    QCOMPARE(snapshot.entries.front().arguments.at(0), QStringLiteral("c++"));
    QVERIFY(snapshot.entries.front().arguments.at(1).isEmpty());
    QCOMPARE(snapshot.entries.front().normalized.argv, snapshot.entries.front().arguments);
}

void ContractTest::preservesEmptyV1Arguments() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto path = temporary.filePath(QStringLiteral("snapshot.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"json({
      "schema_version":"buildscope.snapshot/v1",
      "producer":{"name":"buildscope","version":"0.1.0"},
      "source":{"path":"compile_commands.json","entry_count":1},
      "entries":[{"file":"src/main.cpp","directory":"build","arguments":["c++",""],"command":null,"output":null}]
    })json");
    file.close();

    const auto snapshot = buildscope::loadSnapshotFile(path);

    QCOMPARE(snapshot.entries.front().arguments.size(), 2);
    QVERIFY(snapshot.entries.front().arguments.at(1).isEmpty());
}

void ContractTest::acceptsCommandOnlyV2Entry() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto path = temporary.filePath(QStringLiteral("snapshot.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const auto payload = mutateV2Entry([](QJsonObject &entry) {
        entry.insert(QStringLiteral("arguments"), QJsonValue(QJsonValue::Null));
        entry.insert(QStringLiteral("diagnostics"), QJsonArray());
        auto normalized = entry.value(QStringLiteral("normalized")).toObject();
        normalized.insert(QStringLiteral("invocation_source"), QStringLiteral("command"));
        entry.insert(QStringLiteral("normalized"), normalized);
    });
    QCOMPARE(file.write(payload), payload.size());
    file.close();

    const auto snapshot = buildscope::loadSnapshotFile(path);

    QVERIFY(snapshot.entries.front().arguments.isEmpty());
    QCOMPARE(snapshot.entries.front().normalized.invocationSource, QStringLiteral("command"));
    QVERIFY(snapshot.entries.front().diagnostics.isEmpty());
    QCOMPARE(buildscope::invocationText(snapshot.entries.front()),
             QStringLiteral("c++ -std=c++20 -DFEATURE=1 -Iinclude -c src/main.cpp"));
}

void ContractTest::rejectsMalformedV2Core() {
    expectContractError(
        mutateV2Entry([](QJsonObject &entry) { entry.remove(QStringLiteral("normalized")); }),
        QStringLiteral("entries[0].normalized is required"));
    expectContractError(
        mutateV2Entry([](QJsonObject &entry) { entry.insert(QStringLiteral("state"), 1); }),
        QStringLiteral("entries[0].state must be an object"));
    expectContractError(
        mutateV2Entry(
            [](QJsonObject &entry) { entry.insert(QStringLiteral("diagnostics"), true); }),
        QStringLiteral("entries[0].diagnostics must be an array"));
    expectContractError(
        mutateV2Entry([](QJsonObject &entry) {
            auto normalized = entry.value(QStringLiteral("normalized")).toObject();
            normalized.insert(QStringLiteral("compiler"), QStringLiteral("gcc"));
            entry.insert(QStringLiteral("normalized"), normalized);
        }),
        QStringLiteral("entries[0].normalized.compiler must be an object"));
    expectContractError(
        mutateV2Entry([](QJsonObject &entry) {
            auto state = entry.value(QStringLiteral("state")).toObject();
            state.insert(QStringLiteral("source_status"), QStringLiteral("invalid"));
            entry.insert(QStringLiteral("state"), state);
        }),
        QStringLiteral("entries[0].state.source_status is unsupported"));
    expectContractError(
        mutateV2Entry([](QJsonObject &entry) {
            auto diagnostics = entry.value(QStringLiteral("diagnostics")).toArray();
            diagnostics.replace(0, QStringLiteral("warning"));
            entry.insert(QStringLiteral("diagnostics"), diagnostics);
        }),
        QStringLiteral("entries[0].diagnostics[0] must be an object"));
    expectContractError(
        mutateV2Entry([](QJsonObject &entry) {
            entry.insert(QStringLiteral("arguments"), QJsonValue());
            entry.insert(QStringLiteral("command"), QJsonValue());
        }),
        QStringLiteral("entries[0] must contain at least one of arguments or command"));
    expectContractError(
        mutateV2Entry([](QJsonObject &entry) {
            auto arguments = entry.value(QStringLiteral("arguments")).toArray();
            arguments.replace(0, QString());
            entry.insert(QStringLiteral("arguments"), arguments);
        }),
        QStringLiteral("entries[0].arguments[0] must be a non-empty string"));
    expectContractError(
        mutateV2Entry([](QJsonObject &entry) {
            auto normalized = entry.value(QStringLiteral("normalized")).toObject();
            auto argv = normalized.value(QStringLiteral("argv")).toArray();
            argv.replace(1, QStringLiteral("-std=c++17"));
            normalized.insert(QStringLiteral("argv"), argv);
            entry.insert(QStringLiteral("normalized"), normalized);
        }),
        QStringLiteral("entries[0].normalized.argv must match raw arguments"));
    expectContractError(
        mutateV2Entry([](QJsonObject &entry) {
            auto normalized = entry.value(QStringLiteral("normalized")).toObject();
            auto includePaths = normalized.value(QStringLiteral("include_paths")).toArray();
            auto include = includePaths.at(0).toObject();
            include.insert(QStringLiteral("order"), 1);
            includePaths.replace(0, include);
            normalized.insert(QStringLiteral("include_paths"), includePaths);
            entry.insert(QStringLiteral("normalized"), normalized);
        }),
        QStringLiteral("entries[0].normalized.include_paths[0].order must match include path order"));

    expectContractError(
        mutateV2Snapshot([](QJsonObject &root) {
            root.insert(QStringLiteral("unexpected"), true);
        }),
        QStringLiteral("root contains unsupported field unexpected"));
    expectContractError(
        mutateV2Snapshot([](QJsonObject &root) {
            auto producer = root.value(QStringLiteral("producer")).toObject();
            producer.insert(QStringLiteral("unexpected"), true);
            root.insert(QStringLiteral("producer"), producer);
        }),
        QStringLiteral("root.producer contains unsupported field unexpected"));
    expectContractError(
        mutateV2Snapshot([](QJsonObject &root) {
            auto source = root.value(QStringLiteral("source")).toObject();
            source.insert(QStringLiteral("unexpected"), true);
            root.insert(QStringLiteral("source"), source);
        }),
        QStringLiteral("root.source contains unsupported field unexpected"));
    expectContractError(
        mutateV2Entry([](QJsonObject &entry) {
            entry.insert(QStringLiteral("unexpected"), true);
        }),
        QStringLiteral("entries[0] contains unsupported field unexpected"));
    expectContractError(
        mutateV2Entry([](QJsonObject &entry) { entry.remove(QStringLiteral("arguments")); }),
        QStringLiteral("entries[0].arguments is required"));
    expectContractError(
        mutateV2Entry([](QJsonObject &entry) {
            auto arguments = entry.value(QStringLiteral("arguments")).toArray();
            arguments.replace(1, QString(QChar::Null));
            entry.insert(QStringLiteral("arguments"), arguments);
        }),
        QStringLiteral("entries[0].arguments[1] must be a non-empty string"));
    expectContractError(
        mutateV2Normalized([](QJsonObject &normalized) {
            normalized.insert(QStringLiteral("invocation_source"), QStringLiteral("command"));
        }),
        QStringLiteral("invocation_source does not match the raw invocation form"));
    expectContractError(
        mutateV2Normalized([](QJsonObject &normalized) {
            normalized.insert(QStringLiteral("unexpected"), true);
        }),
        QStringLiteral("entries[0].normalized contains unsupported field unexpected"));
    expectContractError(
        mutateV2Normalized([](QJsonObject &normalized) {
            normalized.insert(QStringLiteral("argv"), QJsonArray());
        }),
        QStringLiteral("entries[0].normalized.argv must be a non-empty string array"));
    expectContractError(
        mutateV2Normalized([](QJsonObject &normalized) {
            QJsonArray argv;
            argv.append(1);
            normalized.insert(QStringLiteral("argv"), argv);
        }),
        QStringLiteral("entries[0].normalized.argv[0] must be a string"));
    expectContractError(
        mutateV2Normalized([](QJsonObject &normalized) {
            normalized.insert(QStringLiteral("command_style"), QStringLiteral("unknown"));
        }),
        QStringLiteral("entries[0].normalized.command_style is unsupported"));
    expectContractError(
        mutateV2Normalized([](QJsonObject &normalized) {
            normalized.insert(QStringLiteral("invocation_source"), QStringLiteral("unknown"));
        }),
        QStringLiteral("entries[0].normalized.invocation_source is unsupported"));
    expectContractError(
        mutateV2Normalized([](QJsonObject &normalized) {
            normalized.insert(QStringLiteral("configuration"), QStringLiteral("sha512:bad"));
        }),
        QStringLiteral("entries[0].normalized.configuration must be a sha256 digest"));
    expectContractError(
        mutateV2Normalized([](QJsonObject &normalized) {
            auto digest = normalized.value(QStringLiteral("configuration")).toString();
            digest[7] = QLatin1Char('A');
            normalized.insert(QStringLiteral("configuration"), digest);
        }),
        QStringLiteral("entries[0].normalized.configuration must be a sha256 digest"));
    expectContractError(
        mutateV2Normalized([](QJsonObject &normalized) {
            normalized.insert(QStringLiteral("language"), QStringLiteral("fortran"));
        }),
        QStringLiteral("entries[0].normalized.language is unsupported"));
    expectContractError(
        mutateV2Normalized([](QJsonObject &normalized) {
            normalized.insert(QStringLiteral("language"), 1);
        }),
        QStringLiteral("entries[0].normalized.language must be a string"));
    expectContractError(
        mutateV2Normalized([](QJsonObject &normalized) {
            normalized.insert(QStringLiteral("output"), 1);
        }),
        QStringLiteral("entries[0].normalized.output must be an object or null"));
    expectContractError(
        mutateV2Normalized([](QJsonObject &normalized) {
            normalized.remove(QStringLiteral("output"));
        }),
        QStringLiteral("entries[0].normalized.output must be an object or null"));
    expectContractError(
        mutateV2Normalized([](QJsonObject &normalized) {
            normalized.insert(QStringLiteral("source"), 1);
        }),
        QStringLiteral("entries[0].normalized.source must be an object"));
    expectContractError(
        mutateV2Normalized([](QJsonObject &normalized) {
            normalized.insert(QStringLiteral("standard"), 1);
        }),
        QStringLiteral("entries[0].normalized.standard must be a string"));
    expectContractError(
        mutateV2Normalized([](QJsonObject &normalized) {
            normalized.insert(QStringLiteral("standard"),
                              QString(1024 * 1024 + 1, QLatin1Char('x')));
        }),
        QStringLiteral("entries[0].normalized.standard exceeds the character limit"));
    expectContractError(
        mutateV2Normalized([](QJsonObject &normalized) {
            normalized.insert(QStringLiteral("sysroot"), 1);
        }),
        QStringLiteral("entries[0].normalized.sysroot must be an object or null"));
    expectContractError(
        mutateV2Normalized([](QJsonObject &normalized) {
            normalized.remove(QStringLiteral("sysroot"));
        }),
        QStringLiteral("entries[0].normalized.sysroot must be an object or null"));

    expectContractError(
        mutateV2Compiler([](QJsonObject &compiler) {
            compiler.insert(QStringLiteral("unexpected"), true);
        }),
        QStringLiteral("entries[0].normalized.compiler contains unsupported field unexpected"));
    expectContractError(
        mutateV2Compiler([](QJsonObject &compiler) {
            compiler.insert(QStringLiteral("family"), QStringLiteral("other"));
        }),
        QStringLiteral("entries[0].normalized.compiler.family is unsupported"));
    expectContractError(
        mutateV2Compiler([](QJsonObject &compiler) {
            compiler.insert(QStringLiteral("name"), QString());
        }),
        QStringLiteral("entries[0].normalized.compiler.name must be a non-empty string"));
    expectContractError(
        mutateV2Compiler([](QJsonObject &compiler) {
            compiler.insert(QStringLiteral("path"), 1);
        }),
        QStringLiteral("entries[0].normalized.compiler.path must be a non-empty string"));
    expectContractError(
        mutateV2Compiler([](QJsonObject &compiler) {
            compiler.insert(QStringLiteral("wrappers"), 1);
        }),
        QStringLiteral("entries[0].normalized.compiler.wrappers must be a string array"));
    expectContractError(
        mutateV2Compiler([](QJsonObject &compiler) {
            QJsonArray wrappers;
            wrappers.append(1);
            compiler.insert(QStringLiteral("wrappers"), wrappers);
        }),
        QStringLiteral("entries[0].normalized.compiler.wrappers[0] must be a string"));
    expectContractError(
        mutateV2Compiler([](QJsonObject &compiler) {
            compiler.insert(QStringLiteral("wrappers"), QJsonArray{QString()});
        }),
        QStringLiteral("entries[0].normalized.compiler.wrappers[0] must be a non-empty string"));

    expectContractError(
        mutateV2Path([](QJsonObject &path) {
            path.insert(QStringLiteral("unexpected"), true);
        }),
        QStringLiteral("entries[0].normalized.directory contains unsupported field unexpected"));
    expectContractError(
        mutateV2Path([](QJsonObject &path) {
            path.insert(QStringLiteral("path"), QString());
        }),
        QStringLiteral("entries[0].normalized.directory.path must be a non-empty string"));
    expectContractError(
        mutateV2Path([](QJsonObject &path) {
            path.insert(QStringLiteral("scope"), QStringLiteral("other"));
        }),
        QStringLiteral("entries[0].normalized.directory.scope is unsupported"));
    expectContractError(
        mutateV2Path([](QJsonObject &path) {
            path.insert(QStringLiteral("style"), QStringLiteral("other"));
        }),
        QStringLiteral("entries[0].normalized.directory.style is unsupported"));
    expectContractError(
        mutateV2Path([](QJsonObject &path) {
            path.insert(QStringLiteral("exists"), QStringLiteral("yes"));
        }),
        QStringLiteral("entries[0].normalized.directory.exists must be a boolean or null"));
    expectContractError(
        mutateV2Normalized([](QJsonObject &normalized) {
            normalized.insert(QStringLiteral("directory"), 1);
        }),
        QStringLiteral("entries[0].normalized.directory must be an object"));

    expectContractError(
        mutateV2Define([](QJsonObject &define) {
            define.insert(QStringLiteral("unexpected"), true);
        }),
        QStringLiteral("entries[0].normalized.defines[0] contains unsupported field unexpected"));
    expectContractError(
        mutateV2Normalized([](QJsonObject &normalized) {
            normalized.insert(QStringLiteral("defines"), 1);
        }),
        QStringLiteral("entries[0].normalized.defines must be an array"));
    expectContractError(
        mutateV2Normalized([](QJsonObject &normalized) {
            normalized.insert(QStringLiteral("defines"), QJsonArray{1});
        }),
        QStringLiteral("entries[0].normalized.defines[0] must be an object"));
    expectContractError(
        mutateV2Define([](QJsonObject &define) {
            define.insert(QStringLiteral("action"), QStringLiteral("other"));
        }),
        QStringLiteral("entries[0].normalized.defines[0].action is unsupported"));
    expectContractError(
        mutateV2Define([](QJsonObject &define) {
            define.insert(QStringLiteral("name"), QStringLiteral("bad-name"));
        }),
        QStringLiteral("entries[0].normalized.defines[0].name is not a valid definition name"));
    expectContractError(
        mutateV2Define([](QJsonObject &define) { define.remove(QStringLiteral("value")); }),
        QStringLiteral("entries[0].normalized.defines[0].value must be a string or null"));
    expectContractError(
        mutateV2Define([](QJsonObject &define) { define.insert(QStringLiteral("value"), 1); }),
        QStringLiteral("entries[0].normalized.defines[0].value must be a string or null"));
    expectContractError(
        mutateV2Define([](QJsonObject &define) {
            define.insert(QStringLiteral("value"), QString(QChar::Null));
        }),
        QStringLiteral("entries[0].normalized.defines[0].value must be a string or null"));

    expectContractError(
        mutateV2Normalized([](QJsonObject &normalized) {
            normalized.insert(QStringLiteral("include_paths"), 1);
        }),
        QStringLiteral("entries[0].normalized.include_paths must be an array"));
    expectContractError(
        mutateV2Normalized([](QJsonObject &normalized) {
            normalized.insert(QStringLiteral("include_paths"), QJsonArray{1});
        }),
        QStringLiteral("entries[0].normalized.include_paths[0] must be an object"));
    expectContractError(
        mutateV2Include([](QJsonObject &include) {
            include.insert(QStringLiteral("unexpected"), true);
        }),
        QStringLiteral("entries[0].normalized.include_paths[0] contains unsupported field unexpected"));
    expectContractError(
        mutateV2Include([](QJsonObject &include) {
            include.insert(QStringLiteral("path"), QString());
        }),
        QStringLiteral("entries[0].normalized.include_paths[0].path must be a non-empty string"));
    expectContractError(
        mutateV2Include([](QJsonObject &include) {
            include.insert(QStringLiteral("scope"), QStringLiteral("other"));
        }),
        QStringLiteral("entries[0].normalized.include_paths[0].scope is unsupported"));
    expectContractError(
        mutateV2Include([](QJsonObject &include) {
            include.insert(QStringLiteral("style"), QStringLiteral("other"));
        }),
        QStringLiteral("entries[0].normalized.include_paths[0].style is unsupported"));
    expectContractError(
        mutateV2Include([](QJsonObject &include) {
            include.insert(QStringLiteral("exists"), QStringLiteral("yes"));
        }),
        QStringLiteral("entries[0].normalized.include_paths[0].exists must be a boolean or null"));
    expectContractError(
        mutateV2Include([](QJsonObject &include) {
            include.insert(QStringLiteral("kind"), QStringLiteral("other"));
        }),
        QStringLiteral("entries[0].normalized.include_paths[0].kind is unsupported"));
    expectContractError(
        mutateV2Include([](QJsonObject &include) { include.insert(QStringLiteral("order"), -1); }),
        QStringLiteral("entries[0].normalized.include_paths[0].order must be a non-negative integer"));
    expectContractError(
        mutateV2Include([](QJsonObject &include) { include.insert(QStringLiteral("order"), 1.5); }),
        QStringLiteral("entries[0].normalized.include_paths[0].order must be a non-negative integer"));

    expectContractError(
        mutateV2Target([](QJsonObject &target) {
            target.insert(QStringLiteral("unexpected"), true);
        }),
        QStringLiteral("entries[0].normalized.target contains unsupported field unexpected"));
    expectContractError(
        mutateV2Normalized([](QJsonObject &normalized) {
            normalized.insert(QStringLiteral("target"), 1);
        }),
        QStringLiteral("entries[0].normalized.target must be an object"));
    expectContractError(
        mutateV2Target([](QJsonObject &target) {
            target.insert(QStringLiteral("build_target"), 1);
        }),
        QStringLiteral("entries[0].normalized.target.build_target must be a string"));

    expectContractError(
        mutateV2State([](QJsonObject &state) {
            state.insert(QStringLiteral("unexpected"), true);
        }),
        QStringLiteral("entries[0].state contains unsupported field unexpected"));
    expectContractError(
        mutateV2State([](QJsonObject &state) {
            state.insert(QStringLiteral("duplicate"), QStringLiteral("false"));
        }),
        QStringLiteral("entries[0].state.duplicate must be a boolean"));
    expectContractError(
        mutateV2State([](QJsonObject &state) { state.insert(QStringLiteral("entry_index"), -1); }),
        QStringLiteral("entries[0].state.entry_index must be a non-negative integer"));
    expectContractError(
        mutateV2State([](QJsonObject &state) {
            state.insert(QStringLiteral("source_configuration_count"), 0);
        }),
        QStringLiteral("entries[0].state.source_configuration_count must be positive"));
    expectContractError(
        mutateV2State([](QJsonObject &state) {
            state.insert(QStringLiteral("source_configuration_count"), 2);
        }),
        QStringLiteral("source_configuration_count does not match the entry set"));
    expectContractError(
        mutateV2State([](QJsonObject &state) {
            state.insert(QStringLiteral("duplicate"), true);
        }),
        QStringLiteral("duplicate does not match the entry set"));
    expectContractError(
        mutateV2State([](QJsonObject &state) {
            state.insert(QStringLiteral("entry_index"), 1);
        }),
        QStringLiteral("entry_index must be a unique in-range index"));

    expectContractError(
        mutateV2Diagnostics([](QJsonObject &diagnostic) {
            diagnostic.insert(QStringLiteral("unexpected"), true);
        }),
        QStringLiteral("entries[0].diagnostics[0] contains unsupported field unexpected"));
    expectContractError(
        mutateV2Diagnostics([](QJsonObject &diagnostic) {
            diagnostic.insert(QStringLiteral("code"), 1);
        }),
        QStringLiteral("entries[0].diagnostics[0].code must be a non-empty string"));
    expectContractError(
        mutateV2Diagnostics([](QJsonObject &diagnostic) {
            diagnostic.insert(QStringLiteral("message"), QString());
        }),
        QStringLiteral("entries[0].diagnostics[0].message must be a non-empty string"));
    expectContractError(
        mutateV2Diagnostics([](QJsonObject &diagnostic) {
            diagnostic.insert(QStringLiteral("severity"), QStringLiteral("other"));
        }),
        QStringLiteral("entries[0].diagnostics[0].severity is unsupported"));
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

void ContractTest::rejectsDuplicateJsonKeys() {
    expectContractError(
        R"json({"schema_version":"buildscope.snapshot/v1","schema_version":"buildscope.snapshot/v2"})json",
        QStringLiteral("duplicate JSON object key: schema_version"));
    expectContractError(
        R"json({"schema_version":"buildscope.snapshot/v1","\u0073chema_version":"buildscope.snapshot/v2"})json",
        QStringLiteral("duplicate JSON object key: schema_version"));
}

void ContractTest::rejectsFinalSymlink() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto target = temporary.filePath(QStringLiteral("target.json"));
    QFile file(target);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("{}"), qint64(2));
    file.close();
    const auto link = temporary.filePath(QStringLiteral("link.json"));
    if (!QFile::link(target, link)) {
        QSKIP("symbolic links are unavailable");
    }

    try {
        buildscope::loadSnapshotFile(link);
        QFAIL("expected buildscope::ContractError");
    } catch (const buildscope::ContractError &error) {
        QVERIFY2(QString::fromUtf8(error.what()).contains("symbolic links are forbidden"),
                 error.what());
    }
}

void ContractTest::rejectsOversizedFile() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QFile file(temporary.filePath(QStringLiteral("snapshot.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.resize(256LL * 1024LL * 1024LL + 1));
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
