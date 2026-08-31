#include "contract_json_guard.hpp"

#include "buildscope/contract.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QString>

namespace buildscope::detail {
namespace {

class JsonKeyScanner final {
public:
    explicit JsonKeyScanner(const QByteArray &payload) : payload_(payload) {}

    void scan() { parseValue(0); }

private:
    static constexpr qsizetype kMaxDepth = 512;

    void skipWhitespace() {
        while (index_ < payload_.size() &&
               (payload_.at(index_) == ' ' || payload_.at(index_) == '\t' ||
                payload_.at(index_) == '\r' || payload_.at(index_) == '\n')) {
            ++index_;
        }
    }

    QByteArray parseStringToken() {
        const auto start = index_++;
        while (index_ < payload_.size()) {
            const auto character = payload_.at(index_++);
            if (character == '\\') {
                ++index_;
            } else if (character == '"') {
                break;
            }
        }
        return payload_.mid(start, index_ - start);
    }

    static QString decodedKey(const QByteArray &token) {
        if (!token.contains('\\')) {
            return QString::fromUtf8(token.constData() + 1, token.size() - 2);
        }
        const auto document = QJsonDocument::fromJson('[' + token + ']');
        return document.array().at(0).toString();
    }

    void parseObject(qsizetype depth) {
        ++index_;
        skipWhitespace();
        QSet<QString> keys;
        while (index_ < payload_.size() && payload_.at(index_) != '}') {
            const auto key = decodedKey(parseStringToken());
            if (keys.contains(key)) {
                throw ContractError("duplicate JSON object key: " + key);
            }
            keys.insert(key);
            skipWhitespace();
            ++index_;  // colon; QJsonDocument already established valid syntax.
            parseValue(depth + 1);
            skipWhitespace();
            if (index_ < payload_.size() && payload_.at(index_) == ',') {
                ++index_;
                skipWhitespace();
            }
        }
        ++index_;
    }

    void parseArray(qsizetype depth) {
        ++index_;
        skipWhitespace();
        while (index_ < payload_.size() && payload_.at(index_) != ']') {
            parseValue(depth + 1);
            skipWhitespace();
            if (index_ < payload_.size() && payload_.at(index_) == ',') {
                ++index_;
                skipWhitespace();
            }
        }
        ++index_;
    }

    void parsePrimitive() {
        while (index_ < payload_.size() && payload_.at(index_) != ',' &&
               payload_.at(index_) != ']' && payload_.at(index_) != '}' &&
               payload_.at(index_) != ' ' && payload_.at(index_) != '\t' &&
               payload_.at(index_) != '\r' && payload_.at(index_) != '\n') {
            ++index_;
        }
    }

    void parseValue(qsizetype depth) {
        if (depth > kMaxDepth) {
            throw ContractError("JSON nesting exceeds 512 level limit");
        }
        skipWhitespace();
        if (index_ >= payload_.size()) {
            return;
        }
        if (payload_.at(index_) == '{') {
            parseObject(depth);
        } else if (payload_.at(index_) == '[') {
            parseArray(depth);
        } else if (payload_.at(index_) == '"') {
            parseStringToken();
        } else {
            parsePrimitive();
        }
    }

    const QByteArray &payload_;
    qsizetype index_ = 0;
};

}  // namespace

void rejectDuplicateJsonKeys(const QByteArray &payload) {
    JsonKeyScanner(payload).scan();
}

}  // namespace buildscope::detail
