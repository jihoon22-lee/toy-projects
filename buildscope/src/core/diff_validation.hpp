#pragma once

#include "buildscope/diff.hpp"

#include <QJsonObject>
#include <QJsonValue>

#include <optional>

namespace buildscope::diff_validation {

std::optional<DiffConfiguration> parseConfiguration(const QJsonObject &unit,
                                                    const QString &key,
                                                    const QString &location);
DiffSource parseSource(const QJsonObject &unit, const QString &location);
DiffChange parseChange(const QJsonValue &value, const QString &location);
void validateUnitShape(const DiffUnit &unit, const QString &location);

}  // namespace buildscope::diff_validation
