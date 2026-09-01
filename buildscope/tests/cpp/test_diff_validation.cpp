#include "buildscope/contract.hpp"
#include "buildscope/diff.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

namespace {

QJsonObject loadFixtureObject() {
    QFile file(QStringLiteral(BUILDSCOPE_SAMPLE_DIFF));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

QString writeObject(const QJsonObject &object, QTemporaryDir &temporary,
                    const QString &name) {
    const auto path = temporary.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    file.close();
    return path;
}

QString contractError(const QString &path) {
    try {
        (void)buildscope::loadDiffFile(path);
    } catch (const buildscope::ContractError &error) {
        return QString::fromUtf8(error.what());
    }
    return {};
}

QJsonObject unitAt(const QJsonObject &root, qsizetype index) {
    return root.value(QStringLiteral("units")).toArray().at(index).toObject();
}

void replaceUnit(QJsonObject &root, qsizetype index, const QJsonObject &unit) {
    auto units = root.value(QStringLiteral("units")).toArray();
    units.replace(index, unit);
    root.insert(QStringLiteral("units"), units);
}

}  // namespace

class DiffValidationTest final : public QObject {
    Q_OBJECT

private slots:
    void rejectsMalformedEnvelopeAndPolicy();
    void rejectsMalformedSemanticStructures();
};

void DiffValidationTest::rejectsMalformedEnvelopeAndPolicy() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    qsizetype sequence = 0;
    const auto rejects = [&](QJsonObject root, const QString &expected) {
        const auto path = writeObject(
            root, temporary,
            QStringLiteral("invalid-envelope-%1.json").arg(sequence++));
        const auto message = contractError(path);
        QVERIFY2(message.contains(expected), qPrintable(message));
    };

    auto root = loadFixtureObject();
    root.insert(QStringLiteral("schema_version"), QStringLiteral("buildscope.diff/v9"));
    rejects(root, QStringLiteral("schema_version is unsupported"));

    root = loadFixtureObject();
    auto producer = root.value(QStringLiteral("producer")).toObject();
    producer.insert(QStringLiteral("name"), QStringLiteral("not-buildscope"));
    root.insert(QStringLiteral("producer"), producer);
    rejects(root, QStringLiteral("producer.name is unsupported"));

    root = loadFixtureObject();
    auto inputs = root.value(QStringLiteral("inputs")).toObject();
    auto after = inputs.value(QStringLiteral("after")).toObject();
    after.insert(QStringLiteral("label"), QString(257, QLatin1Char('x')));
    inputs.insert(QStringLiteral("after"), after);
    root.insert(QStringLiteral("inputs"), inputs);
    rejects(root, QStringLiteral("label exceeds 256 characters"));

    root = loadFixtureObject();
    inputs = root.value(QStringLiteral("inputs")).toObject();
    after = inputs.value(QStringLiteral("after")).toObject();
    after.insert(QStringLiteral("semantic_digest"), QStringLiteral("SHA256:bad"));
    inputs.insert(QStringLiteral("after"), after);
    root.insert(QStringLiteral("inputs"), inputs);
    rejects(root, QStringLiteral("lowercase sha256 digest"));

    root = loadFixtureObject();
    inputs = root.value(QStringLiteral("inputs")).toObject();
    after = inputs.value(QStringLiteral("after")).toObject();
    after.insert(QStringLiteral("source_count"), 4);
    inputs.insert(QStringLiteral("after"), after);
    root.insert(QStringLiteral("inputs"), inputs);
    rejects(root, QStringLiteral("source_count exceeds configuration_count"));

    root = loadFixtureObject();
    auto policy = root.value(QStringLiteral("policy")).toObject();
    auto ignored = policy.value(QStringLiteral("ignored_fields")).toArray();
    ignored.replace(0, 7);
    policy.insert(QStringLiteral("ignored_fields"), ignored);
    root.insert(QStringLiteral("policy"), policy);
    rejects(root, QStringLiteral("must be a non-empty string"));

    root = loadFixtureObject();
    policy = root.value(QStringLiteral("policy")).toObject();
    ignored = policy.value(QStringLiteral("ignored_fields")).toArray();
    ignored.replace(1, ignored.at(0));
    policy.insert(QStringLiteral("ignored_fields"), ignored);
    root.insert(QStringLiteral("policy"), policy);
    rejects(root, QStringLiteral("ignored_fields contains a duplicate"));

    root = loadFixtureObject();
    policy = root.value(QStringLiteral("policy")).toObject();
    ignored = policy.value(QStringLiteral("ignored_fields")).toArray();
    ignored.replace(0, QStringLiteral("different policy"));
    policy.insert(QStringLiteral("ignored_fields"), ignored);
    root.insert(QStringLiteral("policy"), policy);
    rejects(root, QStringLiteral("does not match diff policy v1"));

    root = loadFixtureObject();
    policy = root.value(QStringLiteral("policy")).toObject();
    policy.insert(QStringLiteral("suppression_rules"), QJsonArray{7});
    root.insert(QStringLiteral("policy"), policy);
    rejects(root, QStringLiteral("suppression_rules[0] must be an object"));

    root = loadFixtureObject();
    policy = root.value(QStringLiteral("policy")).toObject();
    policy.insert(QStringLiteral("suppression_rules"),
                  QJsonArray{QJsonObject{{QStringLiteral("category"),
                                          QStringLiteral("unknown")},
                                        {QStringLiteral("path"),
                                         QStringLiteral("*.cpp")}}});
    root.insert(QStringLiteral("policy"), policy);
    rejects(root, QStringLiteral("category is unsupported"));

    root = loadFixtureObject();
    policy = root.value(QStringLiteral("policy")).toObject();
    policy.insert(QStringLiteral("suppression_rules"),
                  QJsonArray{QJsonObject{{QStringLiteral("category"),
                                          QStringLiteral("standard")},
                                        {QStringLiteral("path"),
                                         QString(1025, QLatin1Char('x'))}}});
    root.insert(QStringLiteral("policy"), policy);
    rejects(root, QStringLiteral("path exceeds 1024 characters"));

    root = loadFixtureObject();
    policy = root.value(QStringLiteral("policy")).toObject();
    policy.insert(QStringLiteral("suppression_rules"),
                  QJsonArray{QJsonObject{{QStringLiteral("category"),
                                          QStringLiteral("standard")},
                                        {QStringLiteral("path"),
                                         QStringLiteral("src\\bad.cpp")}}});
    root.insert(QStringLiteral("policy"), policy);
    rejects(root, QStringLiteral("supports only"));

    root = loadFixtureObject();
    policy = root.value(QStringLiteral("policy")).toObject();
    const auto rule = QJsonObject{{QStringLiteral("category"),
                                   QStringLiteral("standard")},
                                  {QStringLiteral("path"),
                                   QStringLiteral("*.cpp")}};
    policy.insert(QStringLiteral("suppression_rules"), QJsonArray{rule, rule});
    root.insert(QStringLiteral("policy"), policy);
    rejects(root, QStringLiteral("suppression_rules contains a duplicate"));

    root = loadFixtureObject();
    policy = root.value(QStringLiteral("policy")).toObject();
    policy.insert(
        QStringLiteral("suppression_rules"),
        QJsonArray{QJsonObject{{QStringLiteral("category"), QStringLiteral("target")},
                               {QStringLiteral("path"), QStringLiteral("*.cpp")}},
                   QJsonObject{{QStringLiteral("category"), QStringLiteral("standard")},
                               {QStringLiteral("path"), QStringLiteral("*.cpp")}}});
    root.insert(QStringLiteral("policy"), policy);
    rejects(root, QStringLiteral("not in canonical order"));

    root = loadFixtureObject();
    policy = root.value(QStringLiteral("policy")).toObject();
    policy.insert(QStringLiteral("version"), QStringLiteral("buildscope.diff-policy/v9"));
    root.insert(QStringLiteral("policy"), policy);
    rejects(root, QStringLiteral("policy.version is unsupported"));

    root = loadFixtureObject();
    root.insert(QStringLiteral("diagnostics"), QJsonArray{7});
    rejects(root, QStringLiteral("diagnostics[0] must be an object"));

    root = loadFixtureObject();
    root.insert(
        QStringLiteral("diagnostics"),
        QJsonArray{QJsonObject{{QStringLiteral("code"), QStringLiteral("x")},
                               {QStringLiteral("message"), QStringLiteral("x")},
                               {QStringLiteral("severity"), QStringLiteral("fatal")},
                               {QStringLiteral("source"), QJsonValue::Null}}});
    rejects(root, QStringLiteral("severity"));

    root = loadFixtureObject();
    root.insert(QStringLiteral("units"), QJsonArray{7});
    rejects(root, QStringLiteral("units[0] must be an object"));

    root = loadFixtureObject();
    auto unit = unitAt(root, 2);
    unit.insert(QStringLiteral("suppressed"), true);
    replaceUnit(root, 2, unit);
    rejects(root, QStringLiteral("suppressed does not match its changes"));

    root = loadFixtureObject();
    inputs = root.value(QStringLiteral("inputs")).toObject();
    after = inputs.value(QStringLiteral("after")).toObject();
    after.insert(QStringLiteral("configuration_count"), 4);
    inputs.insert(QStringLiteral("after"), after);
    root.insert(QStringLiteral("inputs"), inputs);
    rejects(root, QStringLiteral("configuration counts do not match summary"));

    const auto arrayPath = temporary.filePath(QStringLiteral("array-root.json"));
    QFile arrayFile(arrayPath);
    QVERIFY(arrayFile.open(QIODevice::WriteOnly));
    arrayFile.write(QByteArrayLiteral("[]"));
    arrayFile.close();
    QCOMPARE(contractError(arrayPath), QStringLiteral("diff report root must be an object"));
}

void DiffValidationTest::rejectsMalformedSemanticStructures() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    qsizetype sequence = 0;
    const auto rejects = [&](QJsonObject root, const QString &expected) {
        const auto path = writeObject(
            root, temporary,
            QStringLiteral("invalid-semantic-%1.json").arg(sequence++));
        const auto message = contractError(path);
        QVERIFY2(message.contains(expected), qPrintable(message));
    };

    auto root = loadFixtureObject();
    auto unit = unitAt(root, 2);
    unit.remove(QStringLiteral("after"));
    replaceUnit(root, 2, unit);
    rejects(root, QStringLiteral("units[2].after is required"));

    root = loadFixtureObject();
    unit = unitAt(root, 2);
    unit.insert(QStringLiteral("after"), QStringLiteral("invalid"));
    replaceUnit(root, 2, unit);
    rejects(root, QStringLiteral("after must be an object or null"));

    root = loadFixtureObject();
    unit = unitAt(root, 2);
    auto after = unit.value(QStringLiteral("after")).toObject();
    after.insert(QStringLiteral("entry_index"), 100001);
    unit.insert(QStringLiteral("after"), after);
    replaceUnit(root, 2, unit);
    rejects(root, QStringLiteral("entry_index exceeds 100000"));

    root = loadFixtureObject();
    unit = unitAt(root, 2);
    after = unit.value(QStringLiteral("after")).toObject();
    auto semantic = after.value(QStringLiteral("semantic")).toObject();
    semantic.insert(QStringLiteral("compiler"), QStringLiteral("invalid"));
    after.insert(QStringLiteral("semantic"), semantic);
    unit.insert(QStringLiteral("after"), after);
    replaceUnit(root, 2, unit);
    rejects(root, QStringLiteral("semantic.compiler must be an object"));

    root = loadFixtureObject();
    unit = unitAt(root, 2);
    after = unit.value(QStringLiteral("after")).toObject();
    semantic = after.value(QStringLiteral("semantic")).toObject();
    auto compiler = semantic.value(QStringLiteral("compiler")).toObject();
    compiler.insert(QStringLiteral("wrappers"), QStringLiteral("ccache"));
    semantic.insert(QStringLiteral("compiler"), compiler);
    after.insert(QStringLiteral("semantic"), semantic);
    unit.insert(QStringLiteral("after"), after);
    replaceUnit(root, 2, unit);
    rejects(root, QStringLiteral("compiler.wrappers must be an array"));

    root = loadFixtureObject();
    unit = unitAt(root, 2);
    after = unit.value(QStringLiteral("after")).toObject();
    semantic = after.value(QStringLiteral("semantic")).toObject();
    semantic.insert(QStringLiteral("flags"), QJsonArray{QJsonObject{}});
    after.insert(QStringLiteral("semantic"), semantic);
    unit.insert(QStringLiteral("after"), after);
    replaceUnit(root, 2, unit);
    rejects(root, QStringLiteral("flags[0] must be a string"));

    root = loadFixtureObject();
    unit = unitAt(root, 2);
    after = unit.value(QStringLiteral("after")).toObject();
    semantic = after.value(QStringLiteral("semantic")).toObject();
    semantic.insert(QStringLiteral("defines"), QJsonArray{7});
    after.insert(QStringLiteral("semantic"), semantic);
    unit.insert(QStringLiteral("after"), after);
    replaceUnit(root, 2, unit);
    rejects(root, QStringLiteral("defines[0] must be an object"));

    root = loadFixtureObject();
    unit = unitAt(root, 2);
    after = unit.value(QStringLiteral("after")).toObject();
    semantic = after.value(QStringLiteral("semantic")).toObject();
    semantic.insert(
        QStringLiteral("defines"),
        QJsonArray{QJsonObject{{QStringLiteral("action"), QStringLiteral("define")},
                               {QStringLiteral("name"), QStringLiteral("not-valid!")},
                               {QStringLiteral("value"), QStringLiteral("1")}}});
    after.insert(QStringLiteral("semantic"), semantic);
    unit.insert(QStringLiteral("after"), after);
    replaceUnit(root, 2, unit);
    rejects(root, QStringLiteral("not a valid macro identifier"));

    root = loadFixtureObject();
    unit = unitAt(root, 2);
    after = unit.value(QStringLiteral("after")).toObject();
    semantic = after.value(QStringLiteral("semantic")).toObject();
    semantic.insert(QStringLiteral("include_paths"), QJsonArray{7});
    after.insert(QStringLiteral("semantic"), semantic);
    unit.insert(QStringLiteral("after"), after);
    replaceUnit(root, 2, unit);
    rejects(root, QStringLiteral("include_paths[0] must be an object"));

    root = loadFixtureObject();
    unit = unitAt(root, 2);
    after = unit.value(QStringLiteral("after")).toObject();
    semantic = after.value(QStringLiteral("semantic")).toObject();
    semantic.insert(QStringLiteral("language"), QStringLiteral("fortran"));
    after.insert(QStringLiteral("semantic"), semantic);
    unit.insert(QStringLiteral("after"), after);
    replaceUnit(root, 2, unit);
    rejects(root, QStringLiteral("language is unsupported"));

    root = loadFixtureObject();
    unit = unitAt(root, 2);
    auto source = unit.value(QStringLiteral("source")).toObject();
    source.insert(QStringLiteral("before"), QJsonValue::Null);
    source.insert(QStringLiteral("after"), QJsonValue::Null);
    unit.insert(QStringLiteral("source"), source);
    replaceUnit(root, 2, unit);
    rejects(root, QStringLiteral("must retain at least one source path"));

    root = loadFixtureObject();
    unit = unitAt(root, 2);
    auto changes = unit.value(QStringLiteral("changes")).toArray();
    changes.replace(0, 7);
    unit.insert(QStringLiteral("changes"), changes);
    replaceUnit(root, 2, unit);
    rejects(root, QStringLiteral("changes[0] must be an object"));

    root = loadFixtureObject();
    unit = unitAt(root, 2);
    changes = unit.value(QStringLiteral("changes")).toArray();
    auto change = changes.at(0).toObject();
    change.remove(QStringLiteral("before"));
    changes.replace(0, change);
    unit.insert(QStringLiteral("changes"), changes);
    replaceUnit(root, 2, unit);
    rejects(root, QStringLiteral("changes[0].before is required"));

    root = loadFixtureObject();
    unit = unitAt(root, 2);
    changes = unit.value(QStringLiteral("changes")).toArray();
    change = changes.at(0).toObject();
    change.insert(QStringLiteral("category"), QStringLiteral("unknown"));
    changes.replace(0, change);
    unit.insert(QStringLiteral("changes"), changes);
    replaceUnit(root, 2, unit);
    rejects(root, QStringLiteral("category is unsupported"));

    root = loadFixtureObject();
    unit = unitAt(root, 2);
    changes = unit.value(QStringLiteral("changes")).toArray();
    change = changes.at(0).toObject();
    change.insert(QStringLiteral("suppression"), QString(1025, QLatin1Char('x')));
    changes.replace(0, change);
    unit.insert(QStringLiteral("changes"), changes);
    replaceUnit(root, 2, unit);
    rejects(root, QStringLiteral("suppression exceeds 1024 characters"));

    root = loadFixtureObject();
    unit = unitAt(root, 2);
    changes = unit.value(QStringLiteral("changes")).toArray();
    change = changes.at(0).toObject();
    change.insert(QStringLiteral("suppression"), QStringLiteral("flag:*.cpp"));
    changes.replace(0, change);
    unit.insert(QStringLiteral("changes"), changes);
    replaceUnit(root, 2, unit);
    rejects(root, QStringLiteral("present exactly when suppressed is true"));

    root = loadFixtureObject();
    unit = unitAt(root, 2);
    unit.insert(QStringLiteral("changes"), QJsonArray{});
    replaceUnit(root, 2, unit);
    rejects(root, QStringLiteral("changes must not be empty"));

    root = loadFixtureObject();
    unit = unitAt(root, 1);
    source = unit.value(QStringLiteral("source")).toObject();
    source.insert(QStringLiteral("before"), QStringLiteral("src/old.cpp"));
    unit.insert(QStringLiteral("source"), source);
    replaceUnit(root, 1, unit);
    rejects(root, QStringLiteral("added unit sides are inconsistent"));

    root = loadFixtureObject();
    unit = unitAt(root, 3);
    changes = unit.value(QStringLiteral("changes")).toArray();
    change = changes.at(0).toObject();
    change.insert(QStringLiteral("after"), QJsonObject{});
    changes.replace(0, change);
    unit.insert(QStringLiteral("changes"), changes);
    replaceUnit(root, 3, unit);
    rejects(root, QStringLiteral("does not match the removed unit"));

    root = loadFixtureObject();
    unit = unitAt(root, 0);
    source = unit.value(QStringLiteral("source")).toObject();
    source.insert(QStringLiteral("after"), source.value(QStringLiteral("before")));
    unit.insert(QStringLiteral("source"), source);
    replaceUnit(root, 0, unit);
    rejects(root, QStringLiteral("moved unit identity is inconsistent"));

    root = loadFixtureObject();
    unit = unitAt(root, 2);
    changes = unit.value(QStringLiteral("changes")).toArray();
    auto first = changes.takeAt(0);
    changes.insert(1, first);
    unit.insert(QStringLiteral("changes"), changes);
    replaceUnit(root, 2, unit);
    rejects(root, QStringLiteral("not in canonical semantic order"));
}

QTEST_APPLESS_MAIN(DiffValidationTest)

#include "test_diff_validation.moc"
