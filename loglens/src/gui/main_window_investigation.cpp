#include "loglens/gui/main_window.hpp"

#include "loglens/gui/log_model.hpp"

#include <QByteArray>
#include <QCheckBox>
#include <QFileDialog>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QTableView>

#include <algorithm>
#include <cstddef>
#include <string>

namespace {

constexpr qsizetype kMaxExportBytes = 16 * 1024 * 1024;

class BoundedJsonWriter {
public:
    explicit BoundedJsonWriter(const QString& path) : file_(path) {
        file_.setDirectWriteFallback(false);
    }

    bool open() { return file_.open(QIODevice::WriteOnly); }

    bool append(const QByteArray& part) {
        if (part.size() > kMaxExportBytes - written_bytes_) {
            too_large_ = true;
            return false;
        }
        if (file_.write(part) != static_cast<qint64>(part.size())) return false;
        written_bytes_ += part.size();
        return true;
    }

    bool commit() { return file_.commit(); }
    void cancel() { file_.cancelWriting(); }
    QString errorString() const { return file_.errorString(); }
    bool tooLarge() const { return too_large_; }

private:
    QSaveFile file_;
    qsizetype written_bytes_ = 0;
    bool too_large_ = false;
};

QString utf8(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

std::string bytes(const QString& value) {
    const QByteArray encoded = value.toUtf8();
    return std::string(encoded.constData(), static_cast<std::size_t>(encoded.size()));
}

const loglens::TriageEntry* findEntry(const loglens::TriageState& state,
                                     const std::string& sourcePath,
                                     std::size_t line) {
    const auto found = std::find_if(state.entries.begin(), state.entries.end(),
                                    [&](const auto& entry) {
        return entry.source_path == sourcePath && entry.line_number == line;
    });
    return found == state.entries.end() ? nullptr : &*found;
}

bool sameSourcePath(const QString& destination, const QString& source) {
    if (source.isEmpty()) return false;
    const QFileInfo destinationInfo(destination);
    const QFileInfo sourceInfo(source);
    if (destinationInfo.absoluteFilePath() == sourceInfo.absoluteFilePath()) return true;
    const QString destinationCanonical = destinationInfo.canonicalFilePath();
    const QString sourceCanonical = sourceInfo.canonicalFilePath();
    return !destinationCanonical.isEmpty() && destinationCanonical == sourceCanonical;
}

QJsonObject diagnosticJson(const loglens::ParseDiagnostic& diagnostic) {
    return QJsonObject{
        {QStringLiteral("code"),
         QString::fromLatin1(loglens::parseDiagnosticCodeName(diagnostic.code))},
        {QStringLiteral("field"), utf8(diagnostic.field)},
        {QStringLiteral("offset"), QString::number(
             static_cast<qulonglong>(diagnostic.offset))},
        {QStringLiteral("message"), utf8(diagnostic.message)},
    };
}

QString base64(const std::string& value) {
    return QString::fromLatin1(
        QByteArray(value.data(), static_cast<int>(value.size())).toBase64());
}

QJsonObject recordJson(const loglens::LogRecord& record,
                       const loglens::TriageEntry* triage) {
    QJsonArray diagnostics;
    for (const auto& diagnostic : record.diagnostics) {
        diagnostics.push_back(diagnosticJson(diagnostic));
    }
    return QJsonObject{
        {QStringLiteral("annotation"), triage == nullptr ? QString() : utf8(triage->annotation)},
        {QStringLiteral("bookmarked"), triage != nullptr && triage->bookmarked},
        {QStringLiteral("diagnostics"), diagnostics},
        {QStringLiteral("input_bytes"),
         QString::number(static_cast<qulonglong>(record.input_bytes))},
        {QStringLiteral("level"), QString::fromLatin1(loglens::levelName(record.level))},
        {QStringLiteral("line_number"),
         QString::number(static_cast<qulonglong>(record.line_number))},
        {QStringLiteral("message"), utf8(record.message)},
        {QStringLiteral("message_base64"), base64(record.message)},
        {QStringLiteral("omitted_bytes"),
         QString::number(static_cast<qulonglong>(record.omitted_bytes))},
        {QStringLiteral("parse_status"),
         QString::fromLatin1(loglens::parseStatusName(record.parse_status))},
        {QStringLiteral("raw_base64"), base64(record.raw)},
        {QStringLiteral("source"), utf8(record.source)},
        {QStringLiteral("source_base64"), base64(record.source)},
        {QStringLiteral("timestamp_ms"),
         QString::number(static_cast<qulonglong>(record.timestamp_ms))},
    };
}

} // namespace

void MainWindow::refreshRecordDetail() {
    const QModelIndex current = table_->currentIndex();
    const loglens::LogRecord* record = model_->recordAt(current.row());
    const bool available = record != nullptr && !currentPath_.isEmpty();
    bookmarkBox_->setEnabled(available);
    annotationEdit_->setEnabled(available);
    if (!available) {
        recordDetail_->clear();
        const QSignalBlocker bookmarkBlocker(bookmarkBox_);
        const QSignalBlocker annotationBlocker(annotationEdit_);
        bookmarkBox_->setChecked(false);
        annotationEdit_->clear();
        return;
    }
    QString detail = tr("Location: %1:%2\nTimestamp (ms): %3\nLevel: %4\nSource: %5\n"
                        "Parse status: %6\nInput/omitted bytes: %7/%8\n")
        .arg(currentPath_)
        .arg(static_cast<qulonglong>(record->line_number))
        .arg(static_cast<qulonglong>(record->timestamp_ms))
        .arg(QString::fromLatin1(loglens::levelName(record->level)))
        .arg(utf8(record->source))
        .arg(QString::fromLatin1(loglens::parseStatusName(record->parse_status)))
        .arg(static_cast<qulonglong>(record->input_bytes))
        .arg(static_cast<qulonglong>(record->omitted_bytes));
    for (const auto& diagnostic : record->diagnostics) {
        detail += tr("Diagnostic: %1 field=%2 byte=%3 — %4\n")
                      .arg(QString::fromLatin1(
                          loglens::parseDiagnosticCodeName(diagnostic.code)))
                      .arg(utf8(diagnostic.field))
                      .arg(static_cast<qulonglong>(diagnostic.offset))
                      .arg(utf8(diagnostic.message));
    }
    detail += tr("\nParsed message:\n%1\n\nRaw evidence:\n%2")
                  .arg(utf8(record->message), utf8(record->raw));
    recordDetail_->setPlainText(detail);
    const loglens::TriageEntry* entry =
        findEntry(triageState_, bytes(currentPath_), record->line_number);
    const QSignalBlocker bookmarkBlocker(bookmarkBox_);
    const QSignalBlocker annotationBlocker(annotationEdit_);
    bookmarkBox_->setChecked(entry != nullptr && entry->bookmarked);
    annotationEdit_->setText(entry == nullptr ? QString() : utf8(entry->annotation));
}

void MainWindow::saveRecordTriage() {
    const loglens::LogRecord* record = model_->recordAt(table_->currentIndex().row());
    if (record == nullptr || currentPath_.isEmpty()) {
        updateStatus(tr("Select a source record before saving a note"));
        return;
    }
    loglens::TriageState next = triageState_;
    loglens::PersistenceError error;
    if (!loglens::setTriageEntry(
            next,
            {bytes(currentPath_), record->line_number, bookmarkBox_->isChecked(),
             bytes(annotationEdit_->text())},
            error)) {
        showPersistenceError(tr("save record triage"), error);
        return;
    }
    if (!writeTriageWorkflow(next, tr("save record triage"))) return;
    triageState_ = std::move(next);
    model_->setTriageState(triageState_, currentPath_);
    updateStatus(tr("Saved triage for %1:%2")
                     .arg(currentPath_)
                     .arg(static_cast<qulonglong>(record->line_number)));
}

bool MainWindow::exportSelectedRows(const QString& path) {
    if (sameSourcePath(path, currentPath_)) {
        updateStatus(tr("Cannot export selection over the open source log"));
        return false;
    }
    QModelIndexList selected = table_->selectionModel()->selectedRows();
    std::sort(selected.begin(), selected.end(), [](const QModelIndex& left,
                                                   const QModelIndex& right) {
        return left.row() < right.row();
    });
    if (selected.empty()) {
        updateStatus(tr("Select at least one record to export"));
        return false;
    }
    BoundedJsonWriter output(path);
    if (!output.open()) {
        updateStatus(tr("Cannot export selection to %1: %2").arg(path, output.errorString()));
        return false;
    }
    QJsonArray sourceWrapper;
    sourceWrapper.push_back(currentPath_);
    QByteArray encodedSource = QJsonDocument(sourceWrapper).toJson(QJsonDocument::Compact);
    encodedSource.remove(0, 1);
    encodedSource.chop(1);
    const auto failWrite = [&]() {
        const QString error = output.tooLarge()
                                  ? tr("Cannot export selection: output exceeds the 16 MiB limit")
                                  : tr("Cannot export selection to %1: %2")
                                        .arg(path, output.errorString());
        output.cancel();
        updateStatus(error);
        return false;
    };
    if (!output.append(QByteArrayLiteral("{\"records\":["))) {
        return failWrite();
    }

    const std::string sourcePath = bytes(currentPath_);
    qsizetype recordCount = 0;
    for (const QModelIndex& index : selected) {
        const loglens::LogRecord* record = model_->recordAt(index.row());
        if (record == nullptr) continue;
        const loglens::TriageEntry* triage =
            findEntry(triageState_, sourcePath, record->line_number);
        const QByteArray encoded =
            QJsonDocument(recordJson(*record, triage)).toJson(QJsonDocument::Compact);
        if (recordCount != 0 && !output.append(QByteArrayLiteral(","))) {
            return failWrite();
        }
        if (!output.append(encoded)) {
            return failWrite();
        }
        ++recordCount;
    }
    if (recordCount == 0) {
        output.cancel();
        updateStatus(tr("Cannot export selection: selected records are no longer available"));
        return false;
    }
    const QByteArray trailer =
        QByteArrayLiteral("],\"schema\":\"loglens.selection/v1\",\"source_path\":") +
        encodedSource + QByteArrayLiteral("}\n");
    if (!output.append(trailer)) {
        return failWrite();
    }
    if (!output.commit()) {
        updateStatus(tr("Cannot export selection to %1: %2").arg(path, output.errorString()));
        return false;
    }
    updateStatus(tr("Exported %1 selected record(s) to %2")
                     .arg(recordCount)
                     .arg(path));
    return true;
}

void MainWindow::chooseExportPath() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export selected records"), QString(), tr("LogLens JSON (*.json)"));
    if (!path.isEmpty()) exportSelectedRows(path);
}
