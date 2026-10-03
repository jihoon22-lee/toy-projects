#include "canonical_json.hpp"

#include "native_error.hpp"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

namespace buildscope::native {
namespace {

void escapeString(const QString &value, QString &out) {
    out += QLatin1Char('"');
    for (const QChar character : value) {
        const ushort code = character.unicode();
        switch (code) {
        case '"':
            out += QStringLiteral("\\\"");
            break;
        case '\\':
            out += QStringLiteral("\\\\");
            break;
        case '\b':
            out += QStringLiteral("\\b");
            break;
        case '\f':
            out += QStringLiteral("\\f");
            break;
        case '\n':
            out += QStringLiteral("\\n");
            break;
        case '\r':
            out += QStringLiteral("\\r");
            break;
        case '\t':
            out += QStringLiteral("\\t");
            break;
        default:
            if (code < 0x20) {
                out += QStringLiteral("\\u%1").arg(code, 4, 16, QLatin1Char('0'));
            } else {
                out += character;
            }
            break;
        }
    }
    out += QLatin1Char('"');
}

QStringList sortedKeys(const QJsonObject &object) {
    QStringList keys = object.keys();
    std::sort(keys.begin(), keys.end(), [](const QString &first, const QString &second) {
        return first.toUtf8() < second.toUtf8();
    });
    return keys;
}

void writeValue(const QJsonValue &value, QString &out, bool pretty, int depth) {
    switch (value.type()) {
    case QJsonValue::Null:
        out += QStringLiteral("null");
        return;
    case QJsonValue::Bool:
        out += value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
        return;
    case QJsonValue::String:
        escapeString(value.toString(), out);
        return;
    case QJsonValue::Double: {
        const double number = value.toDouble();
        if (!std::isfinite(number)) {
            throw NativeError(QStringLiteral("non-standard JSON number is forbidden"));
        }
        const double integral = std::floor(number);
        if (number == integral && std::abs(number) < 9.007199254740992e15) {
            out += QString::number(static_cast<qint64>(number));
        } else {
            out += QString::number(number, 'g', 17);
            if (out.endsWith(QLatin1Char('.'))) {
                out += QLatin1Char('0');
            }
        }
        return;
    }
    case QJsonValue::Array: {
        const QJsonArray array = value.toArray();
        if (array.isEmpty()) {
            out += QStringLiteral("[]");
            return;
        }
        out += QLatin1Char('[');
        const QString indent = pretty ? QString((depth + 1) * 2, QLatin1Char(' ')) : QString();
        for (qsizetype index = 0; index < array.size(); ++index) {
            if (index != 0) {
                out += QLatin1Char(',');
            }
            if (pretty) {
                out += QLatin1Char('\n') + indent;
            }
            writeValue(array.at(index), out, pretty, depth + 1);
        }
        if (pretty) {
            out += QLatin1Char('\n') + QString(depth * 2, QLatin1Char(' '));
        }
        out += QLatin1Char(']');
        return;
    }
    case QJsonValue::Object: {
        const QJsonObject object = value.toObject();
        if (object.isEmpty()) {
            out += QStringLiteral("{}");
            return;
        }
        const QStringList keys = sortedKeys(object);
        out += QLatin1Char('{');
        const QString indent = pretty ? QString((depth + 1) * 2, QLatin1Char(' ')) : QString();
        bool first = true;
        for (const QString &key : keys) {
            if (!first) {
                out += QLatin1Char(',');
            }
            first = false;
            if (pretty) {
                out += QLatin1Char('\n') + indent;
            }
            escapeString(key, out);
            out += QLatin1Char(':');
            if (pretty) {
                out += QLatin1Char(' ');
            }
            writeValue(object.value(key), out, pretty, depth + 1);
        }
        if (pretty) {
            out += QLatin1Char('\n') + QString(depth * 2, QLatin1Char(' '));
        }
        out += QLatin1Char('}');
        return;
    }
    case QJsonValue::Undefined:
        throw NativeError(QStringLiteral("undefined JSON value cannot be serialized"));
    }
    throw NativeError(QStringLiteral("unsupported JSON value cannot be serialized"));
}

}  // namespace

QString dumpsJson(const QJsonValue &value, bool pretty) {
    QString rendered;
    writeValue(value, rendered, pretty, 0);
    return rendered;
}

QString canonicalDigest(const QJsonValue &value) {
    const QByteArray encoded = dumpsJson(value, false).toUtf8();
    return QStringLiteral("sha256:") +
           QString::fromLatin1(QCryptographicHash::hash(encoded, QCryptographicHash::Sha256).toHex());
}

}  // namespace buildscope::native
