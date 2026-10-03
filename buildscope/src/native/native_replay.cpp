#include "native_replay.hpp"
#include "native_analysis.hpp"
#include <QElapsedTimer>

#include "native_command.hpp"
#include "native_error.hpp"

#include <QDateTime>
#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <csignal>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>

namespace buildscope::native {
namespace {

const QRegularExpression kCompilerName(
    QStringLiteral("^(?:(?:[A-Za-z0-9_+.]+-)+)?(?:gcc|g\\+\\+|clang|clang\\+\\+|cc|c\\+\\+)"
                   "(?:-[0-9][A-Za-z0-9_.-]*)?(?:\\.exe)?$"),
    QRegularExpression::CaseInsensitiveOption);

const QSet<QString> kDropExact = {
    QStringLiteral("-c"),  QStringLiteral("-S"),
    QStringLiteral("-E"),  QStringLiteral("-fsyntax-only"),
    QStringLiteral("-M"),  QStringLiteral("-MM"),
    QStringLiteral("-MD"), QStringLiteral("-MMD"),
    QStringLiteral("-MP"), QStringLiteral("-MG"),
    QStringLiteral("-H"),  QStringLiteral("-pipe"),
    QStringLiteral("-v"),  QStringLiteral("--verbose"),
};
const QSet<QString> kDropValue = {
    QStringLiteral("-o"),
    QStringLiteral("--output"),
    QStringLiteral("-MF"),
    QStringLiteral("-MT"),
    QStringLiteral("-MQ"),
    QStringLiteral("-MJ"),
    QStringLiteral("-dumpdir"),
    QStringLiteral("-dumpbase"),
    QStringLiteral("-serialize-diagnostics"),
    QStringLiteral("--serialize-diagnostics"),
    QStringLiteral("-dependency-file"),
    QStringLiteral("--dependency-file"),
};
const QStringList kDropJoined = {
    QStringLiteral("--output="),
    QStringLiteral("-MF"),
    QStringLiteral("-MT"),
    QStringLiteral("-MQ"),
    QStringLiteral("-MJ"),
    QStringLiteral("-save-temps="),
    QStringLiteral("--save-temps="),
    QStringLiteral("-ftime-trace="),
    QStringLiteral("-fdiagnostics-color="),
    QStringLiteral("-dependency-file="),
    QStringLiteral("--dependency-file="),
};
const QSet<QString> kPreserveValue = {
    QStringLiteral("-D"),        QStringLiteral("-U"),
    QStringLiteral("-I"),        QStringLiteral("-F"),
    QStringLiteral("-x"),        QStringLiteral("--language"),
    QStringLiteral("-std"),      QStringLiteral("-include"),
    QStringLiteral("-imacros"),  QStringLiteral("-isystem"),
    QStringLiteral("-iquote"),   QStringLiteral("-idirafter"),
    QStringLiteral("-isysroot"), QStringLiteral("--sysroot"),
    QStringLiteral("-target"),   QStringLiteral("--target"),
    QStringLiteral("-arch"),     QStringLiteral("-march"),
    QStringLiteral("-mcpu"),     QStringLiteral("-mtune"),
    QStringLiteral("-mabi"),     QStringLiteral("-resource-dir"),
};
const QSet<QString> kSafeLanguages = {
    QStringLiteral("c"),           QStringLiteral("c-header"),
    QStringLiteral("c++"),         QStringLiteral("c++-header"),
    QStringLiteral("objective-c"), QStringLiteral("objective-c++"),
};
const QSet<QString> kSafeExact = {
    QStringLiteral("-ansi"),
    QStringLiteral("-pedantic"),
    QStringLiteral("-pedantic-errors"),
    QStringLiteral("-pthread"),
    QStringLiteral("-nostdinc"),
    QStringLiteral("-nostdinc++"),
    QStringLiteral("-undef"),
    QStringLiteral("-trigraphs"),
    QStringLiteral("-Qunused-arguments"),
    QStringLiteral("-fPIC"),
    QStringLiteral("-fPIE"),
    QStringLiteral("-fpic"),
    QStringLiteral("-fpie"),
    QStringLiteral("-fexceptions"),
    QStringLiteral("-fno-exceptions"),
    QStringLiteral("-frtti"),
    QStringLiteral("-fno-rtti"),
    QStringLiteral("-fpermissive"),
    QStringLiteral("-ffreestanding"),
    QStringLiteral("-fhosted"),
    QStringLiteral("-fshort-enums"),
    QStringLiteral("-fshort-wchar"),
    QStringLiteral("-fsigned-char"),
    QStringLiteral("-funsigned-char"),
    QStringLiteral("-fvisibility-inlines-hidden"),
    QStringLiteral("-fno-visibility-inlines-hidden"),
    QStringLiteral("-fconcepts"),
    QStringLiteral("-fconcepts-ts"),
    QStringLiteral("-fcoroutines"),
    QStringLiteral("-fcoroutines-ts"),
    QStringLiteral("-fms-extensions"),
    QStringLiteral("-fstrict-aliasing"),
    QStringLiteral("-fno-strict-aliasing"),
    QStringLiteral("-fcommon"),
    QStringLiteral("-fno-common"),
};
const QStringList kSafePrefixes = {
    QStringLiteral("-D"),
    QStringLiteral("-U"),
    QStringLiteral("-I"),
    QStringLiteral("-F"),
    QStringLiteral("-W"),
    QStringLiteral("-std="),
    QStringLiteral("--std="),
    QStringLiteral("-isystem"),
    QStringLiteral("-iquote"),
    QStringLiteral("-idirafter"),
    QStringLiteral("-imacros"),
    QStringLiteral("--sysroot="),
    QStringLiteral("-isysroot="),
    QStringLiteral("-target="),
    QStringLiteral("--target="),
    QStringLiteral("-arch="),
    QStringLiteral("-march="),
    QStringLiteral("-mcpu="),
    QStringLiteral("-mtune="),
    QStringLiteral("-mabi="),
    QStringLiteral("-resource-dir="),
    QStringLiteral("-stdlib="),
    QStringLiteral("-fabi-version="),
    QStringLiteral("-fconstexpr-"),
    QStringLiteral("-fmacro-prefix-map="),
    QStringLiteral("-ffile-prefix-map="),
    QStringLiteral("-fdebug-prefix-map="),
    QStringLiteral("-fms-compatibility-version="),
    QStringLiteral("-fno-builtin-"),
};
const QSet<QString> kRejectExact = {
    QStringLiteral("-Xclang"),      QStringLiteral("-Xpreprocessor"),
    QStringLiteral("-Xassembler"),  QStringLiteral("-Xlinker"),
    QStringLiteral("-mllvm"),       QStringLiteral("-load"),
    QStringLiteral("-load-plugin"), QStringLiteral("-plugin"),
    QStringLiteral("-cc1"),         QStringLiteral("-wrapper"),
    QStringLiteral("--config"),     QStringLiteral("-B"),
    QStringLiteral("-specs"),
};
const QStringList kRejectPrefixes = {
    QStringLiteral("-Xclang="),
    QStringLiteral("-Xpreprocessor="),
    QStringLiteral("-Xassembler="),
    QStringLiteral("-Xlinker="),
    QStringLiteral("-mllvm="),
    QStringLiteral("-Wa,"),
    QStringLiteral("-Wl,"),
    QStringLiteral("-Wp,"),
    QStringLiteral("-fplugin"),
    QStringLiteral("-fpass-plugin"),
    QStringLiteral("-fmodules"),
    QStringLiteral("-fmodule-"),
    QStringLiteral("-fdump-"),
    QStringLiteral("-fopt-info"),
    QStringLiteral("-fprofile-"),
    QStringLiteral("-ftest-coverage"),
    QStringLiteral("-fpath-coverage"),
    QStringLiteral("-fsanitize="),
    QStringLiteral("-fno-sanitize="),
    QStringLiteral("-save-temps"),
    QStringLiteral("--save-temps"),
    QStringLiteral("-ftime-"),
    QStringLiteral("--config="),
    QStringLiteral("--gcc-toolchain"),
    QStringLiteral("--vfsoverlay"),
    QStringLiteral("-vfsoverlay"),
    QStringLiteral("--coverage"),
    QStringLiteral("-coverage"),
    QStringLiteral("-B"),
    QStringLiteral("-specs="),
};

bool startsWithAny(const QString &token, const QStringList &prefixes) {
    for (const QString &prefix : prefixes) {
        if (token.startsWith(prefix)) {
            return true;
        }
    }
    return false;
}

bool shouldDrop(const QString &token) {
    return kDropExact.contains(token) || startsWithAny(token, kDropJoined) ||
           (token.startsWith(QLatin1String("-g")) && token != QLatin1String("-Winvalid-pch"));
}

bool isRejected(const QString &token) {
    return kRejectExact.contains(token) || startsWithAny(token, kRejectPrefixes);
}

bool isSafe(const QString &token) {
    static const QSet<QString> optimization = {
        QStringLiteral("-O"),  QStringLiteral("-O0"), QStringLiteral("-O1"),
        QStringLiteral("-O2"), QStringLiteral("-O3"), QStringLiteral("-Og"),
        QStringLiteral("-Os"), QStringLiteral("-Oz"), QStringLiteral("-Ofast")};
    return optimization.contains(token) || kSafeExact.contains(token) ||
           startsWithAny(token, kSafePrefixes);
}

bool isWithin(const std::filesystem::path &path, const std::filesystem::path &root) {
    const auto relative = path.lexically_relative(root);
    return !relative.empty() && *relative.begin() != "..";
}

bool regularExecutable(const std::filesystem::path &path) {
    struct stat metadata{};
    if (::stat(path.c_str(), &metadata) != 0) {
        return false;
    }
    return S_ISREG(metadata.st_mode) && ::access(path.c_str(), X_OK) == 0;
}

QString resolveCompiler(const QString &value, const std::filesystem::path &projectRoot) {
    const std::filesystem::path lexical = value.toStdString();
    const QString name = QString::fromStdString(lexical.filename().string());
    if (!kCompilerName.match(name).hasMatch() ||
        (!lexical.is_absolute() && lexical != lexical.filename())) {
        throw IncludeAnalysisError(
            QStringLiteral("only a direct GCC/Clang driver name can be replayed"));
    }
    const QString discovered = QStandardPaths::findExecutable(
        name, {QStringLiteral("/bin"), QStringLiteral("/usr/bin")});
    if (discovered.isEmpty()) {
        throw IncludeAnalysisError(
            QStringLiteral("the compiler driver is unavailable on the system PATH"));
    }
    std::error_code error;
    const std::filesystem::path approved =
        std::filesystem::canonical(discovered.toStdString(), error);
    const std::filesystem::path compiler = std::filesystem::canonical(
        lexical.is_absolute() ? lexical : std::filesystem::path(discovered.toStdString()),
        error);
    if (error || compiler != approved || !regularExecutable(compiler) ||
        isWithin(compiler, projectRoot)) {
        throw IncludeAnalysisError(
            QStringLiteral("the compiler driver is not an approved system executable"));
    }
    return QString::fromStdString(compiler.string());
}

std::optional<std::filesystem::path> resolvedOperand(const QString &value,
                                                     const std::filesystem::path &cwd) {
    if (value.isEmpty() || value.contains(QLatin1Char('\0')) ||
        value.startsWith(QLatin1Char('-'))) {
        return std::nullopt;
    }
    try {
        const std::filesystem::path candidate = value.toStdString();
        return std::filesystem::weakly_canonical(candidate.is_absolute() ? candidate
                                                                         : cwd / candidate);
    } catch (...) {
        return std::nullopt;
    }
}

QString requiredValue(const QStringList &argv, int index, const QString &option) {
    if (index + 1 >= argv.size() || argv.at(index + 1).isEmpty() ||
        argv.at(index + 1).contains(QLatin1Char('\0'))) {
        throw IncludeAnalysisError(
            QStringLiteral("compiler option %1 has no bounded value").arg(option));
    }
    return argv.at(index + 1);
}

struct Consumed {
    int next = 0;
    QStringList preserved;
    int sourceCount = 0;
    bool afterSeparator = false;
};

Consumed consumeArgument(const QStringList &argv, int index, const std::filesystem::path &cwd,
                         const std::filesystem::path &source, bool afterSeparator) {
    const QString &token = argv.at(index);
    if (token == QLatin1String("--")) {
        return {index + 1, {}, 0, true};
    }
    if (token.startsWith(QLatin1Char('@')) || token == QLatin1String("-")) {
        throw IncludeAnalysisError(
            QStringLiteral("response files and compiler stdin are not replayed"));
    }
    const auto operand = resolvedOperand(token, cwd);
    if (operand.has_value() && operand.value() == source) {
        return {index + 1, {}, 1, afterSeparator};
    }
    if (afterSeparator || !token.startsWith(QLatin1Char('-'))) {
        throw IncludeAnalysisError(
            QStringLiteral("compiler argv contains an extra input operand"));
    }
    if (kDropValue.contains(token)) {
        requiredValue(argv, index, token);
        return {index + 2, {}, 0, afterSeparator};
    }
    if (shouldDrop(token)) {
        return {index + 1, {}, 0, afterSeparator};
    }
    if (isRejected(token)) {
        throw IncludeAnalysisError(
            QStringLiteral("unsafe compiler option is not replayed: %1").arg(token));
    }
    if (kPreserveValue.contains(token)) {
        const QString value = requiredValue(argv, index, token);
        if ((token == QLatin1String("-x") || token == QLatin1String("--language")) &&
            !kSafeLanguages.contains(value)) {
            throw IncludeAnalysisError(
                QStringLiteral("unsupported compiler language: %1").arg(value));
        }
        return {index + 2, {token, value}, 0, afterSeparator};
    }
    if (!isSafe(token)) {
        throw IncludeAnalysisError(
            QStringLiteral("compiler option is outside the replay allowlist: %1").arg(token));
    }
    return {index + 1, {token}, 0, afterSeparator};
}

QString baseNameOf(const QString &token) {
    QString replaced = token;
    replaced.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return replaced.mid(replaced.lastIndexOf(QLatin1Char('/')) + 1);
}

std::filesystem::path resolveStrict(const QString &value) {
    std::error_code error;
    const std::filesystem::path resolved =
        std::filesystem::canonical(value.toStdString(), error);
    if (error) {
        throw IncludeAnalysisError(
            QStringLiteral("the compilation directory or source is stale"));
    }
    return resolved;
}

} // namespace

QStringList sanitizedArguments(const QStringList &argv, const QString &cwd,
                               const QString &source) {
    qsizetype characters = 0;
    for (const QString &value : argv) {
        characters += value.size();
    }
    if (argv.size() > kMaxArguments || characters > kMaxArgumentChars) {
        throw IncludeAnalysisError(QStringLiteral("compiler argv exceeds the replay limit"));
    }
    for (const QString &value : argv) {
        if (value.isEmpty() || value.contains(QLatin1Char('\0'))) {
            throw IncludeAnalysisError(
                QStringLiteral("compiler argv contains an invalid argument"));
        }
        // Drivers expand response files before parsing option/value pairs.
        if (value.startsWith(QLatin1Char('@'))) {
            throw IncludeAnalysisError(
                QStringLiteral("response files are not replayed, including option values"));
        }
    }
    const std::filesystem::path cwdPath = cwd.toStdString();
    const std::filesystem::path sourcePath = source.toStdString();
    QStringList kept;
    int sourceOperands = 0;
    int index = 1;
    bool afterSeparator = false;
    while (index < argv.size()) {
        const Consumed consumed =
            consumeArgument(argv, index, cwdPath, sourcePath, afterSeparator);
        index = consumed.next;
        kept.append(consumed.preserved);
        sourceOperands += consumed.sourceCount;
        afterSeparator = consumed.afterSeparator;
    }
    if (sourceOperands != 1) {
        throw IncludeAnalysisError(
            QStringLiteral("compiler argv must identify its source exactly once"));
    }
    return kept;
}

std::tuple<QString, QString> nativeEntryPaths(const QJsonObject &entry,
                                              const QString &projectRoot) {
    const QJsonObject normalized = entry.value(QStringLiteral("normalized")).toObject();
    if (normalized.value(QStringLiteral("command_style")).toString() !=
        QLatin1String("posix")) {
        throw IncludeAnalysisError(
            QStringLiteral("foreign-platform compiler commands cannot be replayed"));
    }
    const QString sourceValue = normalized.value(QStringLiteral("source"))
                                    .toObject()
                                    .value(QStringLiteral("path"))
                                    .toString();
    const QString directoryValue = normalized.value(QStringLiteral("directory"))
                                       .toObject()
                                       .value(QStringLiteral("path"))
                                       .toString();
    const std::filesystem::path directory = directoryValue.toStdString();
    const std::filesystem::path cwdCandidate =
        directory.is_absolute() ? directory
                                : std::filesystem::path(projectRoot.toStdString()) / directory;
    const std::filesystem::path cwd =
        resolveStrict(QString::fromStdString(cwdCandidate.string()));
    const std::filesystem::path sourceLexical = sourceValue.toStdString();
    const std::filesystem::path source = resolveStrict(QString::fromStdString(
        (sourceLexical.is_absolute()
             ? sourceLexical
             : std::filesystem::path(projectRoot.toStdString()) / sourceLexical)
            .string()));
    const std::filesystem::path root = resolveStrict(projectRoot);
    std::error_code error;
    if (!std::filesystem::is_directory(cwd, error) ||
        !std::filesystem::is_regular_file(source, error) || !isWithin(source, root)) {
        throw IncludeAnalysisError(
            QStringLiteral("the compilation source must be a regular project file"));
    }
    return {QString::fromStdString(cwd.string()), QString::fromStdString(source.string())};
}

std::tuple<QStringList, QString, QString> buildTraceCommand(const QJsonObject &entry,
                                                            const QString &projectRoot) {
    resolveStrict(projectRoot);
    const auto [cwd, source] = nativeEntryPaths(entry, projectRoot);
    const QJsonObject normalized = entry.value(QStringLiteral("normalized")).toObject();
    const QStringList argv =
        normalized.value(QStringLiteral("argv")).toVariant().toStringList();
    const QJsonObject compiler = normalized.value(QStringLiteral("compiler")).toObject();
    const QString compilerName = compiler.value(QStringLiteral("name")).toString();
    const QString compilerPath = compiler.value(QStringLiteral("path")).toString();
    int compilerIndex = -1;
    for (int index = 0; index < argv.size(); ++index) {
        if (argv.at(index) == compilerPath || baseNameOf(argv.at(index)) == compilerName) {
            compilerIndex = index;
            break;
        }
    }
    if (compilerIndex < 0) {
        throw IncludeAnalysisError(
            QStringLiteral("normalized compiler is absent from compiler argv"));
    }
    const QStringList compilerArgv = argv.mid(compilerIndex);
    const QString resolved =
        resolveCompiler(compilerArgv.first(), std::filesystem::path(projectRoot.toStdString()));
    const QStringList arguments = sanitizedArguments(compilerArgv, cwd, source);
    QStringList command{resolved};
    command.append(arguments);
    command.append({QStringLiteral("-fdiagnostics-color=never"), QStringLiteral("-w"),
                    QStringLiteral("-E"), QStringLiteral("-H"), QStringLiteral("-o"),
                    QStringLiteral("/dev/null"), source});
    return {command, cwd, source};
}

TraceResult runTraceControlled(const QStringList &command, const QString &cwd) {
    analysisCheckpoint();
    QProcess process;
    process.setProgram(command.first());
    process.setArguments(command.mid(1));
    process.setWorkingDirectory(cwd);
    QProcessEnvironment environment;
    environment.insert(QStringLiteral("LANG"), QStringLiteral("C"));
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    environment.insert(QStringLiteral("PATH"), QStringLiteral("/bin:/usr/bin"));
    environment.insert(QStringLiteral("TERM"), QStringLiteral("dumb"));
    process.setProcessEnvironment(environment);
    process.setStandardInputFile(QProcess::nullDevice());
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.setStandardOutputFile(QProcess::nullDevice());
#if defined(Q_OS_UNIX)
    process.setChildProcessModifier([] { ::setsid(); });
#endif
    auto *control = activeAnalysisControl();
    const qint64 byteLimit = control ? control->limits.traceBytes : kMaxTraceBytes;
    QElapsedTimer timer;
    timer.start();
    TraceResult result;
    QByteArray captured;
    auto killGroup = [&] {
        const auto pid = process.processId();
        if (pid > 0)
            ::kill(-pid, SIGKILL);
        process.kill();
        process.waitForFinished(1000);
    };
    process.start();
    while (process.state() == QProcess::Starting) {
        process.waitForStarted(20);
        try {
            analysisCheckpoint();
        } catch (const IncludeAnalysisError &error) {
            result.stopReason = QString::fromUtf8(error.what());
            killGroup();
            break;
        }
        if (timer.elapsed() >= 5000) {
            result.stopReason = QStringLiteral("compiler start timeout");
            killGroup();
            break;
        }
    }
    if (process.error() == QProcess::FailedToStart && result.stopReason.isEmpty())
        result.stopReason = QStringLiteral("compiler include trace could not start");
    auto drain = [&] {
        const auto remaining = std::max(qint64(0), byteLimit - captured.size());
        const auto available = process.bytesAvailable();
        captured += process.read(std::min(remaining, available));
        if (available > remaining) {
            result.stopReason = QStringLiteral("compiler trace output budget exhausted");
            killGroup();
        }
    };
    process.setReadChannel(QProcess::StandardError);
    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(20);
        drain();
        if (!result.stopReason.isEmpty())
            break;
        try {
            analysisCheckpoint();
        } catch (const IncludeAnalysisError &error) {
            result.stopReason = QString::fromUtf8(error.what());
            killGroup();
            break;
        }
        if (!control && timer.elapsed() >= kTraceTimeoutSeconds * 1000) {
            result.stopReason = QStringLiteral("compiler include trace timed out");
            killGroup();
            break;
        }
    }
    drain();
    result.durationMs = timer.elapsed();
    result.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    if (!result.stopReason.isEmpty()) {
        // A capped stderr tail may contain half a path; only complete lines are evidence.
        const auto newline = captured.lastIndexOf('\n');
        captured.truncate(newline < 0 ? 0 : newline + 1);
    }
    result.text = QString::fromUtf8(captured);
    result.complete = result.stopReason.isEmpty() && result.exitCode == 0;
    return result;
}

std::tuple<int, QString, qint64> runTrace(const QStringList &command, const QString &cwd) {
    auto result = runTraceControlled(command, cwd);
    if (!result.stopReason.isEmpty())
        throw IncludeAnalysisError(result.stopReason);
    return {result.exitCode, result.text, result.durationMs};
}

} // namespace buildscope::native
