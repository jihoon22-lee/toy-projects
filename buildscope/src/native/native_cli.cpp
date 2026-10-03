#include "native_diff.hpp"
#include "native_error.hpp"
#include "native_include.hpp"
#include "native_io.hpp"
#include "native_snapshot.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QTextStream>

namespace {

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
                errorMessage =
                    QStringLiteral("argument %1: expected one argument").arg(name);
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
          << "                  [--schema-version {v1,v2,v3}]\n"
          << "                  [--include-analysis {estimate,compiler,delayed}]\n"
          << "                  [--analysis-unit GLOB] [--analysis-max-units N]\n"
          << "                  [--analysis-time-budget N]\n"
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
    if (!schema.isEmpty() && schema != QLatin1String("v1") &&
        schema != QLatin1String("v2") && schema != QLatin1String("v3")) {
        return fail(error, QStringLiteral("buildscope: error: "),
                    QStringLiteral("argument --schema-version: invalid choice: '%1'")
                        .arg(schema));
    }
    const QString includeMode =
        optionValue(parsed, QStringLiteral("--include-analysis"));
    if (!includeMode.isEmpty() && includeMode != QLatin1String("estimate") &&
        includeMode != QLatin1String("compiler") &&
        includeMode != QLatin1String("delayed")) {
        return fail(error, QStringLiteral("buildscope: error: "),
                    QStringLiteral("argument --include-analysis: invalid choice: '%1'")
                        .arg(includeMode));
    }
    const auto unitGlobs =
        parsed.options.value(QStringLiteral("--analysis-unit"));
    if (!unitGlobs.isEmpty() && includeMode != QLatin1String("delayed")) {
        return fail(error, QStringLiteral("buildscope: error: "),
                    QStringLiteral("--analysis-unit requires "
                                   "--include-analysis delayed"));
    }
    try {
        const QString database =
            QFileInfo(parsed.positional.first()).absoluteFilePath();
        QString schemaName = schema;
        if (schemaName.isEmpty()) {
            schemaName = includeMode.isEmpty() ? QStringLiteral("v2") : QStringLiteral("v3");
        }
        if (!includeMode.isEmpty() && schemaName != QLatin1String("v3")) {
            throw SnapshotError(
                QStringLiteral("--include-analysis requires --schema-version v3"));
        }
        QString mode = includeMode;
        if (schemaName == QLatin1String("v3") && mode.isEmpty()) {
            mode = QStringLiteral("estimate");
        }
        const QString projectRoot = optionValue(parsed, QStringLiteral("--project-root"),
                                                QDir::currentPath());
        bool valid = true;
        const int maxUnits =
            integerValue(parsed, QStringLiteral("--analysis-max-units"),
                         buildscope::native::kDefaultMaxAnalysisUnits, valid);
        const int budget =
            integerValue(parsed, QStringLiteral("--analysis-time-budget"),
                         buildscope::native::kDefaultAnalysisBudgetSeconds, valid);
        if (!valid) {
            throw SnapshotError(QStringLiteral("analysis limits must be integers"));
        }
        QJsonObject snapshot =
            buildscope::native::loadCompilationDatabase(database, projectRoot);
        if (!mode.isEmpty()) {
            buildscope::native::annotateSnapshot(snapshot, projectRoot, mode, maxUnits,
                                                 budget, unitGlobs);
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
        return fail(error, QStringLiteral("buildscope: "),
                    QString::fromStdString(nativeError.what()));
    } catch (const std::exception &genericError) {
        return fail(error, QStringLiteral("buildscope: "),
                    QString::fromUtf8(genericError.what()));
    }
    return 0;
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
            optionValue(parsed, QStringLiteral("--before-label"),
                        QStringLiteral("before")),
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

}  // namespace

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QStringList arguments = app.arguments();
    arguments.removeFirst();
    if (!arguments.isEmpty() && arguments.first() == QLatin1String("diff")) {
        arguments.removeFirst();
        return diffMain(arguments);
    }
    return snapshotMain(arguments);
}
