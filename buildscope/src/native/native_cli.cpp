#include "buildscope/contract.hpp"
#include "buildscope/impact.hpp"
#include "native_diff.hpp"
#include "native_error.hpp"
#include "native_include.hpp"
#include "native_io.hpp"
#include "native_relocation.hpp"
#include "native_snapshot.hpp"
#include <atomic>
#include <csignal>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QTextStream>

namespace {
std::atomic_bool analysisCancelled{false};
void cancelAnalysis(int) { analysisCancelled.store(true); }

using buildscope::native::IncludeAnalysisError;
using buildscope::native::NativeError;
using buildscope::native::SnapshotError;

int fail(QTextStream &error, const QString &prefix, const QString &message) {
    error << prefix << message << "\n";
    return 2;
}

struct ParsedArguments {
    QStringList positional;
    QMap<QString, QStringList> options;
};

// Minimal argparse-compatible option parsing: "--name value", "--name=value",
// "-o value", and boolean flags.
ParsedArguments parseArguments(const QStringList &arguments,
                               const QMap<QString, bool> &takesValue, bool &ok,
                               QString &errorMessage) {
    ParsedArguments result;
    ok = true;
    for (qsizetype index = 0; index < arguments.size(); ++index) {
        const QString token = arguments.at(index);
        if (!token.startsWith(QLatin1Char('-')) || token == QLatin1String("-")) {
            result.positional.append(token);
            continue;
        }
        QString name = token;
        std::optional<QString> inlineValue;
        const qsizetype equals = token.indexOf(QLatin1Char('='));
        if (equals >= 0) {
            name = token.left(equals);
            inlineValue = token.mid(equals + 1);
        }
        const auto found = takesValue.constFind(name);
        if (found == takesValue.constEnd()) {
            ok = false;
            errorMessage = QStringLiteral("unrecognized arguments: %1").arg(token);
            return result;
        }
        if (found.value()) {
            if (inlineValue.has_value()) {
                result.options[name].append(inlineValue.value());
            } else if (index + 1 < arguments.size()) {
                result.options[name].append(arguments.at(++index));
            } else {
                ok = false;
                errorMessage = QStringLiteral("argument %1: expected one argument").arg(name);
                return result;
            }
        } else {
            if (inlineValue.has_value()) {
                ok = false;
                errorMessage = QStringLiteral("argument %1: ignored explicit argument '%2'")
                                   .arg(name, inlineValue.value());
                return result;
            }
            result.options[name].append(QString());
        }
    }
    return result;
}

QString optionValue(const ParsedArguments &arguments, const QString &name,
                    const QString &fallback = QString()) {
    const auto found = arguments.options.constFind(name);
    if (found == arguments.options.constEnd() || found->isEmpty()) {
        return fallback;
    }
    return found->last();
}

bool hasOption(const ParsedArguments &arguments, const QString &name) {
    return arguments.options.contains(name);
}

int integerValue(const ParsedArguments &arguments, const QString &name, int fallback,
                 bool &ok) {
    const auto found = arguments.options.constFind(name);
    if (found == arguments.options.constEnd() || found->isEmpty()) {
        return fallback;
    }
    bool converted = false;
    const int value = found->last().toInt(&converted);
    if (!converted) {
        ok = false;
    }
    return value;
}

void snapshotUsage(QTextStream &error) {
    error << "usage: buildscope [-h] [--version] [--project-root PROJECT_ROOT] [-o OUTPUT]\n"
          << "                  [--schema-version {v1,v2,v3,v4}]\n"
          << "                  [--include-analysis {estimate,compiler,delayed}]\n"
          << "                  [--analysis-unit GLOB] [--analysis-max-units N]\n"
          << "                  [--analysis-time-budget N] [--analysis-unit-ms N]\n"
          << "                  [--analysis-source-bytes N] [--analysis-unit-bytes N]\n"
          << "                  [--analysis-max-files N] [--analysis-unit-files N]\n"
          << "                  [--analysis-max-edges N] [--analysis-unit-edges N]\n"
          << "                  [--analysis-trace-bytes N] [--map-root OLD=NEW]\n"
          << "                  [--pretty] database\n"
          << "       buildscope diff ...\n";
}

int diffMain(const QStringList &arguments);

int snapshotMain(const QStringList &arguments) {
    QTextStream output(stdout);
    QTextStream error(stderr);
    const QMap<QString, bool> takesValue = {
        {QStringLiteral("-h"), false},
        {QStringLiteral("--help"), false},
        {QStringLiteral("--version"), false},
        {QStringLiteral("--project-root"), true},
        {QStringLiteral("-o"), true},
        {QStringLiteral("--output"), true},
        {QStringLiteral("--schema-version"), true},
        {QStringLiteral("--include-analysis"), true},
        {QStringLiteral("--analysis-unit"), true},
        {QStringLiteral("--analysis-max-units"), true},
        {QStringLiteral("--analysis-time-budget"), true},
        {QStringLiteral("--pretty"), false},
        {"--analysis-unit-ms", true},
        {"--analysis-source-bytes", true},
        {"--analysis-unit-bytes", true},
        {"--analysis-max-files", true},
        {"--analysis-unit-files", true},
        {"--analysis-max-edges", true},
        {"--analysis-unit-edges", true},
        {"--analysis-trace-bytes", true},
        {"--map-root", true},
    };
    bool ok = false;
    QString parseError;
    const ParsedArguments parsed = parseArguments(arguments, takesValue, ok, parseError);
    if (!ok) {
        snapshotUsage(error);
        return fail(error, QStringLiteral("buildscope: error: "), parseError);
    }
    if (hasOption(parsed, QStringLiteral("-h")) ||
        hasOption(parsed, QStringLiteral("--help"))) {
        snapshotUsage(output);
        return 0;
    }
    if (hasOption(parsed, QStringLiteral("--version"))) {
        output << "buildscope " BUILDSCOPE_VERSION "\n";
        return 0;
    }
    if (parsed.positional.size() != 1) {
        snapshotUsage(error);
        return fail(error, QStringLiteral("buildscope: error: "),
                    QStringLiteral("the following arguments are required: database"));
    }
    const QString schema = optionValue(parsed, QStringLiteral("--schema-version"));
    if (!schema.isEmpty() && schema != QLatin1String("v1") && schema != QLatin1String("v2") &&
        schema != QLatin1String("v3") && schema != QLatin1String("v4")) {
        return fail(
            error, QStringLiteral("buildscope: error: "),
            QStringLiteral("argument --schema-version: invalid choice: '%1'").arg(schema));
    }
    const QString includeMode = optionValue(parsed, QStringLiteral("--include-analysis"));
    if (!includeMode.isEmpty() && includeMode != QLatin1String("estimate") &&
        includeMode != QLatin1String("compiler") && includeMode != QLatin1String("delayed")) {
        return fail(error, QStringLiteral("buildscope: error: "),
                    QStringLiteral("argument --include-analysis: invalid choice: '%1'")
                        .arg(includeMode));
    }
    const auto unitGlobs = parsed.options.value(QStringLiteral("--analysis-unit"));
    if (!unitGlobs.isEmpty() && includeMode != QLatin1String("delayed")) {
        return fail(error, QStringLiteral("buildscope: error: "),
                    QStringLiteral("--analysis-unit requires "
                                   "--include-analysis delayed"));
    }
    try {
        const QString database = QFileInfo(parsed.positional.first()).absoluteFilePath();
        QString schemaName = schema;
        if (schemaName.isEmpty()) {
            schemaName = includeMode.isEmpty() ? QStringLiteral("v2") : QStringLiteral("v4");
        }
        if (!includeMode.isEmpty() && schemaName != QLatin1String("v3") &&
            schemaName != QLatin1String("v4")) {
            throw SnapshotError(
                QStringLiteral("--include-analysis requires --schema-version v3 or v4"));
        }
        QString mode = includeMode;
        if ((schemaName == QLatin1String("v3") || schemaName == QLatin1String("v4")) &&
            mode.isEmpty()) {
            mode = QStringLiteral("estimate");
        }
        const QString projectRoot =
            optionValue(parsed, QStringLiteral("--project-root"), QDir::currentPath());
        bool valid = true;
        const int maxUnits = integerValue(parsed, QStringLiteral("--analysis-max-units"),
                                          buildscope::native::kDefaultMaxAnalysisUnits, valid);
        const int budget =
            integerValue(parsed, QStringLiteral("--analysis-time-budget"),
                         buildscope::native::kDefaultAnalysisBudgetSeconds, valid);
        if (!valid) {
            throw SnapshotError(QStringLiteral("analysis limits must be integers"));
        }
        const auto mappings =
            buildscope::native::parseRootMappings(parsed.options.value("--map-root"));
        QJsonObject snapshot = buildscope::native::loadCompilationDatabase(
            database, projectRoot, mappings, &analysisCancelled);
        if (!mode.isEmpty()) {
            buildscope::native::AnalysisLimits limits;
            limits.maxUnits = maxUnits;
            if (budget < 1 || budget > 600)
                throw SnapshotError(
                    QStringLiteral("analysis time budget must be 1..600 seconds"));
            limits.totalMilliseconds = budget * 1000;
            limits.unitMilliseconds =
                integerValue(parsed, "--analysis-unit-ms", limits.unitMilliseconds, valid);
            limits.totalFiles =
                integerValue(parsed, "--analysis-max-files", limits.totalFiles, valid);
            limits.unitFiles =
                integerValue(parsed, "--analysis-unit-files", limits.unitFiles, valid);
            limits.totalEdges =
                integerValue(parsed, "--analysis-max-edges", limits.totalEdges, valid);
            limits.unitEdges =
                integerValue(parsed, "--analysis-unit-edges", limits.unitEdges, valid);
            auto bytes = [&](const QString &name, qint64 fallback) {
                if (!hasOption(parsed, name))
                    return fallback;
                bool ok;
                const auto n = optionValue(parsed, name).toLongLong(&ok);
                if (!ok || n < 1)
                    throw SnapshotError(QStringLiteral("invalid byte budget: ") + name);
                return n;
            };
            limits.totalSourceBytes = bytes("--analysis-source-bytes", limits.totalSourceBytes);
            limits.unitSourceBytes = bytes("--analysis-unit-bytes", limits.unitSourceBytes);
            limits.traceBytes = bytes("--analysis-trace-bytes", limits.traceBytes);
            if (!valid)
                throw SnapshotError(QStringLiteral("analysis limits must be integers"));
            buildscope::native::AnalysisControl control(limits, &analysisCancelled);
            buildscope::native::annotateSnapshotControlled(snapshot, projectRoot, mode, control,
                                                           unitGlobs);
        }
        const QString rendered = buildscope::native::dumpsSnapshot(
            buildscope::native::snapshotForSchema(snapshot, schemaName),
            hasOption(parsed, QStringLiteral("--pretty")));
        const QString target = optionValue(parsed, QStringLiteral("--output"),
                                           optionValue(parsed, QStringLiteral("-o")));
        if (target.isEmpty()) {
            output << rendered;
            output.flush();
        } else {
            buildscope::native::writeAtomicText(target, rendered, {database});
        }
    } catch (const NativeError &nativeError) {
        fail(error, QStringLiteral("buildscope: "),QString::fromStdString(nativeError.what()));
        return analysisCancelled.load()?130:2;
    } catch (const std::exception &genericError) {
        fail(error, QStringLiteral("buildscope: "),QString::fromUtf8(genericError.what()));
        return analysisCancelled.load()?130:2;
    }
    return analysisCancelled.load() ? 130 : 0;
}

int impactMain(const QStringList &arguments) {
    QTextStream output(stdout), error(stderr);
    bool ok = false;
    QString message;
    const auto parsed = parseArguments(arguments,
                                       {{"--header", true},
                                        {"--pretty", false},
                                        {"--output", true},
                                        {"-o", true},
                                        {"--help", false}},
                                       ok, message);
    if (hasOption(parsed, "--help")) {
        output << "usage: buildscope impact SNAPSHOT --header PATH [--pretty] [-o FILE]\n";
        return 0;
    }
    if (!ok || parsed.positional.size() != 1 || !hasOption(parsed, "--header"))
        return fail(error, "buildscope impact: ",
                    message.isEmpty() ? "snapshot and --header are required" : message);
    try {
        const auto path = parsed.positional.first();
        const auto snapshot = buildscope::loadSnapshotFile(path,&analysisCancelled);
        auto report = buildscope::includeImpact(snapshot, optionValue(parsed, "--header"),
                                                &analysisCancelled);
        const auto rendered =
            buildscope::native::dumpsSnapshot(report, hasOption(parsed, "--pretty"));
        const auto target = optionValue(parsed, "--output", optionValue(parsed, "-o"));
        if (target.isEmpty())
            output << rendered;
        else
            buildscope::native::writeAtomicText(target, rendered, {path});
        return analysisCancelled.load() ? 130 : 0;
    } catch (const std::exception &e) {
        fail(error, "buildscope impact: ", QString::fromUtf8(e.what()));
        return analysisCancelled.load()?130:2;
    }
}

void diffUsage(QTextStream &error) {
    error << "usage: buildscope diff [-h] [--project-root R] [--before-project-root R]\n"
          << "                       [--after-project-root R] [--before-label L]\n"
          << "                       [--after-label L] [--suppress C[:G]] [-o O]\n"
          << "                       [--pretty] before after\n";
}

int diffMain(const QStringList &arguments) {
    QTextStream output(stdout);
    QTextStream error(stderr);
    const QMap<QString, bool> takesValue = {
        {QStringLiteral("-h"), false},
        {QStringLiteral("--help"), false},
        {QStringLiteral("--project-root"), true},
        {QStringLiteral("--before-project-root"), true},
        {QStringLiteral("--after-project-root"), true},
        {QStringLiteral("--before-label"), true},
        {QStringLiteral("--after-label"), true},
        {QStringLiteral("--suppress"), true},
        {QStringLiteral("-o"), true},
        {QStringLiteral("--output"), true},
        {QStringLiteral("--pretty"), false},
    };
    bool ok = false;
    QString parseError;
    const ParsedArguments parsed = parseArguments(arguments, takesValue, ok, parseError);
    if (!ok) {
        diffUsage(error);
        return fail(error, QStringLiteral("buildscope diff: error: "), parseError);
    }
    if (hasOption(parsed, QStringLiteral("-h")) ||
        hasOption(parsed, QStringLiteral("--help"))) {
        diffUsage(output);
        return 0;
    }
    if (parsed.positional.size() != 2) {
        diffUsage(error);
        return fail(error, QStringLiteral("buildscope diff: error: "),
                    QStringLiteral("the following arguments are required: before, after"));
    }
    try {
        const QString before = QFileInfo(parsed.positional.at(0)).absoluteFilePath();
        const QString after = QFileInfo(parsed.positional.at(1)).absoluteFilePath();
        const QString shared = optionValue(parsed, QStringLiteral("--project-root"));
        const QString beforeRoot =
            optionValue(parsed, QStringLiteral("--before-project-root"), shared);
        const QString afterRoot =
            optionValue(parsed, QStringLiteral("--after-project-root"), shared);
        const QJsonObject report = buildscope::native::compareDatabases(
            before, after, beforeRoot, afterRoot,
            optionValue(parsed, QStringLiteral("--before-label"), QStringLiteral("before")),
            optionValue(parsed, QStringLiteral("--after-label"), QStringLiteral("after")),
            parsed.options.value(QStringLiteral("--suppress")));
        const QString rendered = buildscope::native::dumpsDiff(
            report, hasOption(parsed, QStringLiteral("--pretty")));
        const QString target = optionValue(parsed, QStringLiteral("--output"),
                                           optionValue(parsed, QStringLiteral("-o")));
        if (target.isEmpty()) {
            output << rendered;
            output.flush();
        } else {
            buildscope::native::writeAtomicText(target, rendered, {before, after});
        }
        return report.value(QStringLiteral("summary"))
                           .toObject()
                           .value(QStringLiteral("visible_units"))
                           .toInt() != 0
                   ? 1
                   : 0;
    } catch (const NativeError &nativeError) {
        return fail(error, QStringLiteral("buildscope diff: "),
                    QString::fromStdString(nativeError.what()));
    } catch (const std::exception &genericError) {
        return fail(error, QStringLiteral("buildscope diff: "),
                    QString::fromUtf8(genericError.what()));
    }
}

} // namespace

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    std::signal(SIGINT, cancelAnalysis);
    std::signal(SIGTERM, cancelAnalysis);
    QStringList arguments = app.arguments();
    arguments.removeFirst();
    if (!arguments.isEmpty() && arguments.first() == "impact") {
        arguments.removeFirst();
        return impactMain(arguments);
    }
    if (!arguments.isEmpty() && arguments.first() == QLatin1String("diff")) {
        std::signal(SIGINT,SIG_DFL);std::signal(SIGTERM,SIG_DFL);
        arguments.removeFirst();
        return diffMain(arguments);
    }
    return snapshotMain(arguments);
}
