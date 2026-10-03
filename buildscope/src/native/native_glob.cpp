#include "native_glob.hpp"

#include <QRegularExpression>

namespace buildscope::native {
namespace {

QString globToken(const QString &pattern, qsizetype &index) {
    const QChar character = pattern.at(index);
    if (character != QLatin1Char('*')) {
        ++index;
        if (character == QLatin1Char('?')) {
            return QStringLiteral("[^/]");
        }
        return QRegularExpression::escape(character);
    }
    if (index + 1 >= pattern.size() || pattern.at(index + 1) != QLatin1Char('*')) {
        ++index;
        return QStringLiteral("[^/]*");
    }
    if (index + 2 < pattern.size() && pattern.at(index + 2) == QLatin1Char('/')) {
        index += 3;
        return QStringLiteral("(?:.*/)?");
    }
    index += 2;
    return QStringLiteral(".*");
}

QString globRegularExpression(const QString &pattern) {
    QString pieces = pattern.contains(QLatin1Char('/')) ? QStringLiteral("^")
                                                        : QStringLiteral("^(?:.*/)?");
    qsizetype index = 0;
    while (index < pattern.size()) {
        pieces += globToken(pattern, index);
    }
    return pieces + QLatin1Char('$');
}

}  // namespace

bool globMatches(const QString &path, const QString &pattern, bool windows) {
    QString normalized = path;
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    QString folded = pattern;
    if (windows) {
        normalized = normalized.toLower();
        folded = folded.toLower();
    }
    const QRegularExpression expression(globRegularExpression(folded));
    return expression.match(normalized).hasMatch();
}

}  // namespace buildscope::native
