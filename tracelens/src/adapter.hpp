#pragma once
#include "tracelens/core.hpp"
#include <QJsonObject>
#include <QString>
namespace tracelens {
QJsonObject event_json(const Event &);
QJsonObject snapshot_json(const Report &);
QJsonObject read_snapshot(const QString &);
QJsonObject diff_json(const QJsonObject &, const QJsonObject &);
Source source_from_json(const QJsonObject &);
Evidence evidence_from_json(const QJsonObject &);
void check_output_alias(const QString &, const std::vector<std::string> &inputs);
void save_atomic(const QString &, const QByteArray &, const std::vector<std::string> &inputs);
QString text(const std::string &);
} // namespace tracelens
