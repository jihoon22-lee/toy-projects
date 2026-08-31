#pragma once

#include <QByteArray>

namespace buildscope::detail {

void rejectDuplicateJsonKeys(const QByteArray &payload);

}  // namespace buildscope::detail
