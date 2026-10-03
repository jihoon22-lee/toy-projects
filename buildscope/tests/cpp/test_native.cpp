#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include "native_command.hpp"
#include "native_diff_policy.hpp"
#include "native_glob.hpp"
#include "native_include.hpp"
#include "native_io.hpp"
#include "native_metadata.hpp"
#include "native_normalize.hpp"
#include "native_paths.hpp"
#include "native_replay.hpp"
#include "native_snapshot.hpp"

using namespace buildscope::native;

namespace {

QJsonObject entryObject(const QString &directory, const QString &file,
                        const QString &command) {
    QJsonObject entry;
    entry[QStringLiteral("directory")] = directory;
    entry[QStringLiteral("file")] = file;
    entry[QStringLiteral("command")] = command;
    return entry;
}

}  // namespace

class NativeProducerTest : public QObject {
    Q_OBJECT
private slots:
    void posixSplit();
    void posixSplitErrors();
    void windowsSplit();
    void invocationSource();
    void compilerFamilies();
    void wrappers();
    void posixLexical();
    void windowsLexical();
    void windowsJoin();
    void projectRelative();
    void foreignFilesystem();
    void defines();
    void includeKinds();
    void languageAndStandard();
    void sysrootAndTarget();
    void outputAndTarget();
    void msvcFlags();
    void recordShape();
    void duplicateDetection();
    void canonicalStability();
    void boundedReads();
    void atomicWrite();
    void protectedOutput();
    void rejections();
    void byteOrderMark();
    void schemaProjections();
    void delayedAnalysisSelection();
    void suppressions();
    void glob();
    void sanitization();
};

void NativeProducerTest::posixSplit() {
    QCOMPARE(splitPosixCommand(QStringLiteral("cc -O2 -D X=1 'a b' \"c d\"")),
             QStringList({QStringLiteral("cc"), QStringLiteral("-O2"),
                          QStringLiteral("-D"), QStringLiteral("X=1"),
                          QStringLiteral("a b"), QStringLiteral("c d")}));
    QCOMPARE(splitPosixCommand(QStringLiteral("cc a\\ b")),
             QStringList({QStringLiteral("cc"), QStringLiteral("a b")}));
    QCOMPARE(splitPosixCommand(QStringLiteral("cc # comment\n-O2")),
             QStringList({QStringLiteral("cc")}));
}

void NativeProducerTest::posixSplitErrors() {
    QVERIFY_THROWS_EXCEPTION(
        std::exception, splitPosixCommand(QStringLiteral("cc 'unclosed")));
    QVERIFY_THROWS_EXCEPTION(
        std::exception, splitPosixCommand(QStringLiteral("cc \"unclosed")));
}

void NativeProducerTest::windowsSplit() {
    QCOMPARE(splitWindowsCommand(QStringLiteral("cl.exe /nologo /I\"a b\" x.cpp")),
             QStringList({QStringLiteral("cl.exe"), QStringLiteral("/nologo"),
                          QStringLiteral("/Ia b"), QStringLiteral("x.cpp")}));
    QCOMPARE(splitWindowsCommand(QStringLiteral("\"C:\\t\\cl.exe\" /c")),
             QStringList({QStringLiteral("C:\\t\\cl.exe"), QStringLiteral("/c")}));
    QCOMPARE(splitWindowsCommand(QStringLiteral("cc \"a\\\"b\"")),
             QStringList({QStringLiteral("cc"), QStringLiteral("a\"b")}));
}

void NativeProducerTest::invocationSource() {
    const QJsonObject command =
        entryObject(QStringLiteral("d"), QStringLiteral("f.c"),
                    QStringLiteral("cc -c f.c"));
    const Invocation first = parseInvocation(command, 0);
    QCOMPARE(first.style, QStringLiteral("posix"));
    QCOMPARE(first.argv.size(), 3);

    QJsonObject args = entryObject(QStringLiteral("d"), QStringLiteral("f.c"), {});
    args.remove(QStringLiteral("command"));
    args[QStringLiteral("arguments")] =
        QJsonArray({QStringLiteral("cc"), QStringLiteral("-c")});
    const Invocation second = parseInvocation(args, 0);
    QCOMPARE(second.argv.size(), 2);
    QVERIFY(second.arguments.has_value());

    QJsonObject both = args;
    both[QStringLiteral("command")] = QStringLiteral("cc -c f.c");
    QCOMPARE(parseInvocation(both, 0).argv.size(), 2);

    QJsonObject empty;
    QVERIFY_THROWS_EXCEPTION(std::exception, parseInvocation(empty, 0));
}

void NativeProducerTest::compilerFamilies() {
    QCOMPARE(compilerRecord({QStringLiteral("gcc")})
                 .value(QStringLiteral("family"))
                 .toString(),
             QStringLiteral("gcc"));
    QCOMPARE(compilerRecord({QStringLiteral("clang++-17")})
                 .value(QStringLiteral("family"))
                 .toString(),
             QStringLiteral("clang"));
    QCOMPARE(compilerRecord({QStringLiteral("cl.exe")})
                 .value(QStringLiteral("family"))
                 .toString(),
             QStringLiteral("msvc"));
    QCOMPARE(compilerRecord({QStringLiteral("clang-cl")})
                 .value(QStringLiteral("family"))
                 .toString(),
             QStringLiteral("clang-cl"));
    QCOMPARE(compilerRecord({QStringLiteral("emcc")})
                 .value(QStringLiteral("family"))
                 .toString(),
             QStringLiteral("emscripten"));
    QCOMPARE(compilerRecord({QStringLiteral("mystery")})
                 .value(QStringLiteral("family"))
                 .toString(),
             QStringLiteral("unknown"));
}

void NativeProducerTest::wrappers() {
    const QJsonObject record = compilerRecord(
        {QStringLiteral("ccache"), QStringLiteral("distcc"), QStringLiteral("g++")});
    QCOMPARE(record.value(QStringLiteral("family")).toString(),
             QStringLiteral("gcc"));
    QCOMPARE(record.value(QStringLiteral("wrappers")).toArray(),
             QJsonArray({QStringLiteral("ccache"), QStringLiteral("distcc")}));

    const QJsonObject env = compilerRecord(
        {QStringLiteral("env"), QStringLiteral("-i"), QStringLiteral("CC=gcc"),
         QStringLiteral("sccache"), QStringLiteral("clang")});
    QCOMPARE(env.value(QStringLiteral("wrappers")).toArray(),
             QJsonArray({QStringLiteral("env"), QStringLiteral("sccache")}));
    QCOMPARE(env.value(QStringLiteral("family")).toString(),
             QStringLiteral("clang"));
}

void NativeProducerTest::posixLexical() {
    QCOMPARE(normalizeLexical(QStringLiteral("src/../x.c"), QStringLiteral("/p"),
                              QStringLiteral("posix")),
             QStringLiteral("/p/x.c"));
    QCOMPARE(normalizeLexical(QStringLiteral("./a/./b"), QStringLiteral("/p"),
                              QStringLiteral("posix")),
             QStringLiteral("/p/a/b"));
    QCOMPARE(normalizeLexical(QStringLiteral("/abs/../c"), QStringLiteral("/p"),
                              QStringLiteral("posix")),
             QStringLiteral("/c"));
    QCOMPARE(normalizeLexical(QStringLiteral("//srv/share/x"), QStringLiteral("/p"),
                              QStringLiteral("posix")),
             QStringLiteral("//srv/share/x"));
    QCOMPARE(normalizeLexical(QStringLiteral("a/../../b"), QStringLiteral("/p"),
                              QStringLiteral("posix")),
             QStringLiteral("/b"));
}

void NativeProducerTest::windowsLexical() {
    QCOMPARE(normalizeLexical(QStringLiteral("C:\\a\\..\\b"), QString(),
                              QStringLiteral("windows")),
             QStringLiteral("C:/b"));
    QCOMPARE(normalizeLexical(QStringLiteral("c:rel"), QStringLiteral("C:/base"),
                              QStringLiteral("windows")),
             QStringLiteral("C:/base/rel"));
    QCOMPARE(normalizeLexical(QStringLiteral("\\\\srv\\share\\x"), QString(),
                              QStringLiteral("windows")),
             QStringLiteral("//srv/share/x"));
}

void NativeProducerTest::windowsJoin() {
    QCOMPARE(normalizeLexical(QStringLiteral("\\root"), QStringLiteral("C:/base"),
                              QStringLiteral("windows")),
             QStringLiteral("C:/root"));
    QCOMPARE(normalizeLexical(QStringLiteral("D:/other"), QStringLiteral("C:/base"),
                              QStringLiteral("windows")),
             QStringLiteral("D:/other"));
}

void NativeProducerTest::projectRelative() {
    QCOMPARE(projectRelativeLexical(QStringLiteral("src/a.c"),
                                    QStringLiteral("/p/build"),
                                    QStringLiteral("/p"), QStringLiteral("posix")),
             QStringLiteral("build/src/a.c"));
    QCOMPARE(projectRelativeLexical(QStringLiteral("/p/src/a.c"),
                                    QStringLiteral("/p/build"),
                                    QStringLiteral("/p"), QStringLiteral("posix")),
             QStringLiteral("src/a.c"));
    QCOMPARE(projectRelativeLexical(QStringLiteral("/outside/a.c"),
                                    QStringLiteral("/p/build"),
                                    QStringLiteral("/p"), QStringLiteral("posix")),
             QStringLiteral("/outside/a.c"));
}

void NativeProducerTest::foreignFilesystem() {
    const QJsonObject record =
        pathRecord(QStringLiteral("C:/definitely/missing/f.c"), QStringLiteral("."),
                   QStringLiteral("."), QStringLiteral("windows"),
                   QStringLiteral("source"));
    QVERIFY(record.value(QStringLiteral("exists")).isNull());
}

void NativeProducerTest::defines() {
    const ExtractedMetadata metadata = extractMetadata(
        {QStringLiteral("cc"), QStringLiteral("-DA=1"), QStringLiteral("-D"),
         QStringLiteral("B"), QStringLiteral("-UB"), QStringLiteral("-D=BAD"),
         QStringLiteral("x.c")},
        QStringLiteral("x.c"));
    const QJsonArray defines = metadata.defines;
    QCOMPARE(defines.size(), 3);
    QCOMPARE(defines.at(0).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("A"));
    QCOMPARE(defines.at(0).toObject().value(QStringLiteral("value")).toString(),
             QStringLiteral("1"));
    bool sawUndefine = false;
    for (const QJsonValue &entry : defines) {
        sawUndefine = sawUndefine ||
                      entry.toObject().value(QStringLiteral("action")).toString() ==
                          QStringLiteral("undefine");
    }
    QVERIFY(sawUndefine);
    bool malformed = false;
    for (const QJsonValue &diagnostic : metadata.diagnostics) {
        malformed =
            malformed ||
            diagnostic.toObject().value(QStringLiteral("code")).toString() ==
                QStringLiteral("invalid-define");
    }
    QVERIFY(malformed);
}

void NativeProducerTest::includeKinds() {
    const ExtractedMetadata metadata = extractMetadata(
        {QStringLiteral("cc"), QStringLiteral("-Iinc"), QStringLiteral("-iquote"),
         QStringLiteral("q"), QStringLiteral("-isystem"), QStringLiteral("sys"),
         QStringLiteral("-idirafter"), QStringLiteral("aft"),
         QStringLiteral("-iframework"), QStringLiteral("fw"), QStringLiteral("x.c")},
        QStringLiteral("x.c"));
    QStringList kinds;
    for (const QJsonValue &path : metadata.includePaths) {
        kinds << path.toObject().value(QStringLiteral("kind")).toString();
    }
    QCOMPARE(kinds, QStringList({QStringLiteral("include"), QStringLiteral("quote"),
                                 QStringLiteral("system"), QStringLiteral("after"),
                                 QStringLiteral("framework")}));
}

void NativeProducerTest::languageAndStandard() {
    QCOMPARE(extractMetadata({QStringLiteral("cc"), QStringLiteral("-x"),
                              QStringLiteral("c++"), QStringLiteral("x")},
                             QString())
                 .language,
             QStringLiteral("c++"));
    QCOMPARE(extractMetadata({QStringLiteral("cc"), QStringLiteral("-std=c++17"),
                              QStringLiteral("x")},
                             QString())
                 .standard,
             QStringLiteral("c++17"));
    QCOMPARE(extractMetadata({QStringLiteral("cc"), QStringLiteral("-std"),
                              QStringLiteral("gnu11"), QStringLiteral("x")},
                             QString())
                 .standard,
             QStringLiteral("gnu11"));
}

void NativeProducerTest::sysrootAndTarget() {
    const ExtractedMetadata metadata = extractMetadata(
        {QStringLiteral("cc"), QStringLiteral("--sysroot=/s"),
         QStringLiteral("-isysroot"), QStringLiteral("/i"),
         QStringLiteral("--target=aarch64-linux"), QStringLiteral("x.c")},
        QStringLiteral("x.c"));
    QCOMPARE(metadata.sysroot, QStringLiteral("/i"));
    QCOMPARE(metadata.targetTriple, QStringLiteral("aarch64-linux"));
}

void NativeProducerTest::outputAndTarget() {
    QCOMPARE(outputFromArgv({QStringLiteral("cc"), QStringLiteral("-o"),
                             QStringLiteral("a.o"), QStringLiteral("-o"),
                             QStringLiteral("b.o")}),
             QStringLiteral("b.o"));
    QCOMPARE(outputFromArgv({QStringLiteral("cc"), QStringLiteral("/Foe.obj")}),
             QStringLiteral("e.obj"));
    QCOMPARE(cmakeTarget(QStringLiteral("build/CMakeFiles/core.dir/a.o")),
             QStringLiteral("core"));
    QCOMPARE(cmakeTarget(QStringLiteral("build/a.o")), QString());
}

void NativeProducerTest::msvcFlags() {
    const ExtractedMetadata metadata = extractMetadata(
        {QStringLiteral("cl.exe"), QStringLiteral("/D"), QStringLiteral("X=1"),
         QStringLiteral("/Iinc"), QStringLiteral("/external:I"),
         QStringLiteral("ext"), QStringLiteral("/std:c++20"),
         QStringLiteral("/Foout.obj"), QStringLiteral("x.cpp")},
        QStringLiteral("x.cpp"));
    QCOMPARE(metadata.standard, QStringLiteral("c++20"));
    QCOMPARE(metadata.includePaths.size(), 2);
    QCOMPARE(metadata.includePaths.at(1)
                 .toObject()
                 .value(QStringLiteral("kind"))
                 .toString(),
             QStringLiteral("system"));
}

void NativeProducerTest::recordShape() {
    const QJsonObject normalized =
        normalizeEntry(entryObject(QStringLiteral("/p"), QStringLiteral("a.c"),
                                   QStringLiteral("cc -DA=1 -c a.c")),
                       0, QStringLiteral("/p"), QStringLiteral("/p"));
    const QJsonObject record = normalized.value(QStringLiteral("normalized")).toObject();
    QVERIFY(record.contains(QStringLiteral("configuration")));
    QVERIFY(record.value(QStringLiteral("configuration"))
                .toString()
                .startsWith(QStringLiteral("sha256:")));
    QCOMPARE(record.value(QStringLiteral("command_style")).toString(),
             QStringLiteral("posix"));
    QVERIFY(record.value(QStringLiteral("source")).isObject());
}

void NativeProducerTest::duplicateDetection() {
    QJsonArray entries;
    entries.append(normalizeEntry(entryObject(QStringLiteral("/p"), QStringLiteral("a.c"),
                                              QStringLiteral("cc -c a.c")),
                                  0, QStringLiteral("/p"), QStringLiteral("/p")));
    entries.append(normalizeEntry(entryObject(QStringLiteral("/p"), QStringLiteral("a.c"),
                                              QStringLiteral("cc -c a.c")),
                                  1, QStringLiteral("/p"), QStringLiteral("/p")));
    annotateEntrySets(entries);
    const QJsonObject state = entries.at(1)
                                  .toObject()
                                  .value(QStringLiteral("state"))
                                  .toObject();
    QVERIFY(state.value(QStringLiteral("duplicate")).toBool());
    QCOMPARE(state.value(QStringLiteral("source_configuration_count")).toInt(), 1);
}

void NativeProducerTest::canonicalStability() {
    const QJsonObject first =
        normalizeEntry(entryObject(QStringLiteral("/p"), QStringLiteral("a.c"),
                                   QStringLiteral("cc -DB=2 -DA=1 -c a.c")),
                       0, QStringLiteral("/p"), QStringLiteral("/p"));
    const QJsonObject second =
        normalizeEntry(entryObject(QStringLiteral("/p"), QStringLiteral("a.c"),
                                   QStringLiteral("cc -DB=2 -DA=1 -c a.c")),
                       3, QStringLiteral("/p"), QStringLiteral("/p"));
    QCOMPARE(first.value(QStringLiteral("normalized"))
                 .toObject()
                 .value(QStringLiteral("configuration")),
             second.value(QStringLiteral("normalized"))
                 .toObject()
                 .value(QStringLiteral("configuration")));
}

void NativeProducerTest::boundedReads() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.path() + QStringLiteral("/blob");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("0123456789");
    file.close();
    QCOMPARE(readBoundedRegular(path, 10), QByteArray("0123456789"));
    QVERIFY_THROWS_EXCEPTION(std::exception, readBoundedRegular(path, 5));
    QVERIFY_THROWS_EXCEPTION(std::exception, readBoundedRegular(directory.path(), 5));
}

void NativeProducerTest::atomicWrite() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.path() + QStringLiteral("/in.json");
    const QString target = directory.path() + QStringLiteral("/out.json");
    QFile seed(input);
    QVERIFY(seed.open(QIODevice::WriteOnly));
    seed.write("x");
    seed.close();
    writeAtomicText(target, QStringLiteral("payload"), {input});
    QFile file(target);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(file.readAll()), QStringLiteral("payload"));
}

void NativeProducerTest::protectedOutput() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = directory.path() + QStringLiteral("/in.json");
    QFile seed(input);
    QVERIFY(seed.open(QIODevice::WriteOnly));
    seed.write("x");
    seed.close();
    QVERIFY_THROWS_EXCEPTION(std::exception,
                             writeAtomicText(input, QStringLiteral("y"), {input}));
}

void NativeProducerTest::rejections() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.path() + QStringLiteral("/cc.json");
    auto writePayload = [&path](const QString &payload) {
        QFile file(path);
        const bool opened = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
        QVERIFY(opened);
        file.write(payload.toUtf8());
        file.close();
    };
    writePayload(QStringLiteral("[{\"a\":1,\"a\":2}]"));
    QVERIFY_THROWS_EXCEPTION(std::exception, loadCompilationDatabase(path, {}));
    writePayload(QStringLiteral("{\"not\":\"array\"}"));
    QVERIFY_THROWS_EXCEPTION(std::exception, loadCompilationDatabase(path, {}));
    writePayload(QStringLiteral("[NaN]"));
    QVERIFY_THROWS_EXCEPTION(std::exception, loadCompilationDatabase(path, {}));

    auto errorFor = [&path](const QByteArray &payload) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return QStringLiteral("cannot write payload");
        }
        file.write(payload);
        file.close();
        try {
            (void)loadCompilationDatabase(path, {});
        } catch (const std::exception &error) {
            return QString::fromUtf8(error.what());
        }
        return QString();
    };
    QCOMPARE(errorFor("{\"not\":\"array\"}"),
             QStringLiteral("compilation database root must be an array"));
    QVERIFY(errorFor("not json").startsWith(
        QStringLiteral("cannot read compilation database: ")));
    QVERIFY(!errorFor("not json").contains(QStringLiteral("root must be an array")));
    QCOMPARE(errorFor("[{\"directory\":\"/tmp\",\"file\":\"a.c\",\"command\":\"cc\"},,]"),
             QStringLiteral("cannot read compilation database: empty element in entry array"));
    QCOMPARE(errorFor("[,]"),
             QStringLiteral("cannot read compilation database: empty element in entry array"));
}

void NativeProducerTest::byteOrderMark() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.path() + QStringLiteral("/cc.json");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("\xEF\xBB\xBF");
    file.write(QStringLiteral("[{\"directory\":\"%1\",\"file\":\"a.c\","
                              "\"command\":\"cc -c a.c\"}]")
                   .arg(directory.path())
                   .toUtf8());
    file.close();
    const QJsonObject snapshot = loadCompilationDatabase(path, directory.path());
    QCOMPARE(snapshot.value(QStringLiteral("entries")).toArray().size(), 1);
}

void NativeProducerTest::schemaProjections() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.path() + QStringLiteral("/cc.json");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QStringLiteral("[{\"directory\":\"%1\",\"file\":\"a.c\","
                              "\"command\":\"cc -c a.c\"}]")
                   .arg(directory.path())
                   .toUtf8());
    file.close();
    const QJsonObject snapshot = loadCompilationDatabase(path, directory.path());
    QCOMPARE(snapshotForSchema(QJsonObject(snapshot), QStringLiteral("v1"))
                 .value(QStringLiteral("schema_version"))
                 .toString(),
             QStringLiteral("buildscope.snapshot/v1"));
    QJsonObject analyzed = snapshot;
    annotateSnapshot(analyzed, directory.path(), QStringLiteral("estimate"),
                     kDefaultMaxAnalysisUnits, kDefaultAnalysisBudgetSeconds);
    QCOMPARE(snapshotForSchema(analyzed, QStringLiteral("v3"))
                 .value(QStringLiteral("schema_version"))
                 .toString(),
             QStringLiteral("buildscope.snapshot/v3"));
}

void NativeProducerTest::delayedAnalysisSelection() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    for (const QString &name : {QStringLiteral("a.c"), QStringLiteral("b.c")}) {
        QFile source(directory.path() + QLatin1Char('/') + name);
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("int unit(void) { return 0; }\n");
        source.close();
    }
    const QString path = directory.path() + QStringLiteral("/cc.json");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QStringLiteral("[{\"directory\":\"%1\",\"file\":\"a.c\","
                              "\"command\":\"cc -c a.c\"},"
                              "{\"directory\":\"%1\",\"file\":\"b.c\","
                              "\"command\":\"cc -c b.c\"}]")
                   .arg(directory.path())
                   .toUtf8());
    file.close();
    QJsonObject snapshot = loadCompilationDatabase(path, directory.path());
    annotateSnapshot(snapshot, directory.path(), QStringLiteral("delayed"),
                     kDefaultMaxAnalysisUnits, kDefaultAnalysisBudgetSeconds,
                     {QStringLiteral("a.c")});
    const QJsonArray entries = snapshot.value(QStringLiteral("entries")).toArray();
    QCOMPARE(entries.size(), 2);
    const QJsonObject first =
        entries.at(0).toObject().value(QStringLiteral("include_analysis")).toObject();
    const QJsonObject second =
        entries.at(1).toObject().value(QStringLiteral("include_analysis")).toObject();
    if (first.value(QStringLiteral("evidence")).toString()
        == QLatin1String("compiler-measured")) {
        // cc resolved to a real driver: only the selected unit replays.
        QCOMPARE(second.value(QStringLiteral("evidence")).toString(),
                 QStringLiteral("estimated"));
    } else {
        // Without a usable compiler both keep honest labels: the selected unit
        // stays estimated with a diagnostic, the rest are plain estimates.
        QCOMPARE(first.value(QStringLiteral("evidence")).toString(),
                 QStringLiteral("estimated"));
        QVERIFY(!first.value(QStringLiteral("diagnostics")).toArray().isEmpty());
        QCOMPARE(second.value(QStringLiteral("evidence")).toString(),
                 QStringLiteral("estimated"));
        QVERIFY(second.value(QStringLiteral("diagnostics")).toArray().isEmpty());
    }

    // Without a glob nothing is replayed; delayed degenerates to estimates.
    QJsonObject plain = loadCompilationDatabase(path, directory.path());
    annotateSnapshot(plain, directory.path(), QStringLiteral("delayed"),
                     kDefaultMaxAnalysisUnits, kDefaultAnalysisBudgetSeconds, {});
    for (const QJsonValue &value : plain.value(QStringLiteral("entries")).toArray()) {
        QCOMPARE(value.toObject()
                     .value(QStringLiteral("include_analysis"))
                     .toObject()
                     .value(QStringLiteral("evidence"))
                     .toString(),
                 QStringLiteral("estimated"));
    }
    QVERIFY_THROWS_EXCEPTION(
        std::exception,
        annotateSnapshot(plain, directory.path(), QStringLiteral("bogus"),
                         kDefaultMaxAnalysisUnits, kDefaultAnalysisBudgetSeconds, {}));
}

void NativeProducerTest::suppressions() {
    QVERIFY_THROWS_EXCEPTION(
        std::exception, parseSuppressions({QStringLiteral("nonsense")}));
    QVERIFY_THROWS_EXCEPTION(std::exception,
                             parseSuppressions({QStringLiteral("flag:a"),
                                                QStringLiteral("flag:a")}));
    const QJsonArray rules = parseSuppressions(
        {QStringLiteral("flag:src/*.c"), QStringLiteral("standard")});
    QCOMPARE(rules.size(), 2);
    QCOMPARE(matchingSuppression(rules, QStringLiteral("flag"),
                                 QStringLiteral("src/a.c"),
                                 QStringLiteral("src/a.c"), false),
             QStringLiteral("flag:src/*.c"));
    QVERIFY(matchingSuppression(rules, QStringLiteral("flag"),
                                QStringLiteral("other/a.c"),
                                QStringLiteral("other/a.c"), false)
                .isNull());
}

void NativeProducerTest::glob() {
    QVERIFY(globMatches(QStringLiteral("src/a/b.c"), QStringLiteral("src/**"),
                        false));
    QVERIFY(globMatches(QStringLiteral("src/a.c"), QStringLiteral("*.c"), false));
    QVERIFY(globMatches(QStringLiteral("src/a/b.c"), QStringLiteral("*.c"),
                        false));
    QVERIFY(globMatches(QStringLiteral("a.c"), QStringLiteral("?.c"), false));
    QVERIFY(globMatches(QStringLiteral("C:/x/a.c"), QStringLiteral("c:/x/a.c"),
                        true));
}

void NativeProducerTest::sanitization() {
    const QStringList sanitized = sanitizedArguments(
        {QStringLiteral("cc"), QStringLiteral("-Iinc"), QStringLiteral("-DXX"),
         QStringLiteral("-o"), QStringLiteral("out.o"), QStringLiteral("-c"),
         QStringLiteral("src.c"), QStringLiteral("-fsyntax-only")},
        QStringLiteral("/p"), QStringLiteral("/p/src.c"));
    QVERIFY(!sanitized.contains(QStringLiteral("-o")));
    QVERIFY(!sanitized.contains(QStringLiteral("out.o")));
    QVERIFY(!sanitized.contains(QStringLiteral("src.c")));
    QVERIFY(sanitized.contains(QStringLiteral("-Iinc")));
    QVERIFY(sanitized.contains(QStringLiteral("-DXX")));
}

QTEST_MAIN(NativeProducerTest)
#include "test_native.moc"
