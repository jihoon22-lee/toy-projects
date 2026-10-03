#pragma once

#include <QJsonValue>
#include <QString>

namespace buildscope::native {

// Serialize a JSON value exactly like Python
// json.dumps(value, ensure_ascii=False, allow_nan=False, sort_keys=True) with
// compact (",", ":") separators, or with indent=2 when pretty is requested.
QString dumpsJson(const QJsonValue &value, bool pretty = false);

// Stable SHA-256 identity of a JSON-compatible semantic value, encoded with
// the compact canonical form above.
QString canonicalDigest(const QJsonValue &value);

}  // namespace buildscope::native
