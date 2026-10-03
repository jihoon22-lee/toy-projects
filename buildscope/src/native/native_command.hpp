#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace buildscope::native {

// _command.py: bounded, shell-free parsing of compile database invocations.
QStringList splitWindowsCommand(const QString &command);
QStringList splitPosixCommand(const QString &command);
QString commandStyle(const QJsonObject &entry);

struct Invocation {
    QStringList argv;
    QString style;
    std::optional<QStringList> arguments;
    std::optional<QString> command;
};

Invocation parseInvocation(const QJsonObject &entry, qsizetype index);
QJsonObject compilerRecord(const QStringList &argv);
QString programName(const QString &token);

constexpr qsizetype kMaxArguments = 32768;
constexpr qsizetype kMaxArgumentChars = 1024 * 1024;
constexpr qsizetype kMaxCommandChars = 4 * 1024 * 1024;

}  // namespace buildscope::native
