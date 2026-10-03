#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <tuple>

namespace buildscope::native {

// _replay_policy.py + compiler_replay.py: bounded, shell-free GCC/Clang
// include-trace replay with an allowlisted argument surface.
QStringList sanitizedArguments(const QStringList &argv, const QString &cwd,
                               const QString &source);
std::tuple<QString, QString> nativeEntryPaths(const QJsonObject &entry,
                                              const QString &projectRoot);
std::tuple<QStringList, QString, QString> buildTraceCommand(const QJsonObject &entry,
                                                            const QString &projectRoot);
std::tuple<int, QString, qint64> runTrace(const QStringList &command, const QString &cwd);

constexpr qsizetype kMaxTraceBytes = 16 * 1024 * 1024;
constexpr int kTraceTimeoutSeconds = 15;

}  // namespace buildscope::native
