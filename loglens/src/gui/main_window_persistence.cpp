#include "loglens/gui/main_window.hpp"

#include <QByteArray>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QObject>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStringList>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace {

QString persistenceErrorText(const QString& action,
                             const loglens::PersistenceError& error) {
    QString message = QStringLiteral("Cannot %1: ").arg(action);
    const QString detail = QString::fromStdString(error.message);
    message += detail.isEmpty() ? QObject::tr("unknown persistence error") : detail;
    message += QStringLiteral(" (code %1, byte %2)")
                   .arg(QString::fromLatin1(loglens::persistenceErrorCodeName(error.code)))
                   .arg(static_cast<qulonglong>(error.offset));
    return message;
}

QString utf8String(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

} // namespace

void MainWindow::loadPersistenceState() {
    QStringList errors;
    const loglens::SourceProfileLoadResult profiles =
        loglens::loadSourceProfiles(sourceProfilesPath_.toStdString());
    if (profiles.ok()) {
        sourceProfiles_ = profiles.profiles;
    } else {
        errors.push_back(persistenceErrorText(tr("load source profiles"), profiles.error));
    }

    const loglens::SavedQueryLoadResult queries =
        loglens::loadSavedQueries(savedQueriesPath_.toStdString());
    if (queries.ok()) {
        savedQueries_ = queries.queries;
    } else {
        errors.push_back(persistenceErrorText(tr("load saved queries"), queries.error));
    }

    rebuildSourceProfiles();
    rebuildSavedQueries();
    if (!errors.isEmpty()) {
        updateStatus(errors.join(QStringLiteral("; ")));
    }
}

void MainWindow::rebuildSourceProfiles(const QString& selectedName) {
    const QString requestedName = selectedName.isEmpty()
                                      ? sourceProfile_->currentText()
                                      : selectedName;
    const QSignalBlocker blocker(sourceProfile_);
    sourceProfile_->clear();
    for (const loglens::SourceProfile& profile : sourceProfiles_) {
        sourceProfile_->addItem(utf8String(profile.name));
    }
    if (sourceProfiles_.empty()) {
        sourceProfile_->setEditText(requestedName.isEmpty() ? QStringLiteral("Default")
                                                            : requestedName);
        return;
    }
    int selected = sourceProfile_->findText(requestedName, Qt::MatchExactly);
    if (selected < 0) {
        selected = 0;
    }
    sourceProfile_->setCurrentIndex(selected);
    setProfileControls(sourceProfiles_[static_cast<std::size_t>(selected)]);
}

void MainWindow::rebuildSavedQueries(const QString& selectedName) {
    const QString requestedName = selectedName.isEmpty() ? savedQuery_->currentText()
                                                          : selectedName;
    const QSignalBlocker blocker(savedQuery_);
    savedQuery_->clear();
    for (const loglens::SavedQuery& query : savedQueries_) {
        savedQuery_->addItem(utf8String(query.name));
    }
    if (savedQueries_.empty()) {
        savedQuery_->setEditText(requestedName);
        return;
    }
    int selected = savedQuery_->findText(requestedName, Qt::MatchExactly);
    if (selected < 0) {
        selected = 0;
    }
    savedQuery_->setCurrentIndex(selected);
}

void MainWindow::setProfileControls(const loglens::SourceProfile& profile) {
    const int format =
        sourceFormat_->findData(QString::fromLatin1(loglens::formatName(profile.format)));
    if (format >= 0) {
        sourceFormat_->setCurrentIndex(format);
    }
    const int multiline = multilinePolicy_->findData(
        QString::fromLatin1(loglens::multilinePolicyName(profile.multiline)));
    if (multiline >= 0) {
        multilinePolicy_->setCurrentIndex(multiline);
    }
    const std::size_t bounded = std::min(profile.max_record_bytes,
                                         static_cast<std::size_t>(loglens::kMaxRecordBytes));
    maxRecordBytes_->setValue(static_cast<int>(std::max<std::size_t>(1, bounded)));
}

loglens::Format MainWindow::selectedFormat() const {
    const QString value = sourceFormat_->currentData().toString();
    const std::optional<loglens::Format> parsed =
        loglens::parseFormatName(value.toStdString());
    return parsed.value_or(loglens::Format::Auto);
}

loglens::MultilinePolicy MainWindow::selectedMultilinePolicy() const {
    const QString value = multilinePolicy_->currentData().toString();
    const std::optional<loglens::MultilinePolicy> parsed =
        loglens::parseMultilinePolicyName(value.toStdString());
    return parsed.value_or(loglens::MultilinePolicy::FoldContinuations);
}

loglens::SourceProfile MainWindow::profileFromControls() const {
    const QByteArray name = sourceProfile_->currentText().toUtf8();
    return loglens::SourceProfile{
        std::string(name.constData(), static_cast<std::size_t>(name.size())), selectedFormat(),
        selectedMultilinePolicy(), static_cast<std::size_t>(maxRecordBytes_->value())};
}

void MainWindow::selectSourceProfile(int index) {
    if (index < 0 || index >= static_cast<int>(sourceProfiles_.size())) {
        return;
    }
    setProfileControls(sourceProfiles_[static_cast<std::size_t>(index)]);
}

void MainWindow::applySourceProfile() {
    if (currentPath_.isEmpty()) {
        updateStatus(tr("Source profile is ready; open a log to apply it"));
        return;
    }
    openPath(currentPath_, selectedLoadMode(), static_cast<std::size_t>(tailRecords_->value()));
}

QString MainWindow::storePath(bool profiles) const {
    return profiles ? sourceProfilesPath_ : savedQueriesPath_;
}

bool MainWindow::prepareDefaultStoreDirectory(bool profiles) {
    const bool isDefault = profiles ? sourceProfilesPathIsDefault_ : savedQueriesPathIsDefault_;
    if (!isDefault) {
        return true;
    }
    const QFileInfo info(storePath(profiles));
    const QString parentPath = info.absolutePath();
    QDir parent(parentPath);
    if (parent.exists()) {
        return true;
    }
    if (QDir().mkpath(parentPath)) {
        return true;
    }
    loglens::PersistenceError error;
    error.code = loglens::PersistenceErrorCode::Io;
    error.message = "cannot create the default persistence directory";
    showPersistenceError(profiles ? tr("prepare source profile store")
                                  : tr("prepare saved query store"),
                         error);
    return false;
}

void MainWindow::showPersistenceError(const QString& action,
                                      const loglens::PersistenceError& error) {
    updateStatus(persistenceErrorText(action, error));
}

void MainWindow::saveSourceProfile() {
    const loglens::SourceProfile candidate = profileFromControls();
    if (candidate.name.empty()) {
        loglens::PersistenceError error;
        error.code = loglens::PersistenceErrorCode::InvalidValue;
        error.message = "profile name must not be empty";
        showPersistenceError(tr("save source profile"), error);
        return;
    }
    std::vector<loglens::SourceProfile> next = sourceProfiles_;
    const auto existing = std::find_if(
        next.begin(), next.end(), [&](const loglens::SourceProfile& profile) {
            return profile.name == candidate.name;
        });
    if (existing == next.end()) {
        if (next.size() >= loglens::kMaxPersistedItems) {
            loglens::PersistenceError error;
            error.code = loglens::PersistenceErrorCode::LimitExceeded;
            error.message = "source profile count exceeds 128-item limit";
            showPersistenceError(tr("save source profile"), error);
            return;
        }
        next.push_back(candidate);
    } else {
        *existing = candidate;
    }
    if (!prepareDefaultStoreDirectory(true)) {
        return;
    }
    loglens::PersistenceError error;
    if (!loglens::saveSourceProfiles(storePath(true).toStdString(), next, error)) {
        showPersistenceError(tr("save source profile"), error);
        return;
    }
    std::sort(next.begin(), next.end(), [](const loglens::SourceProfile& left,
                                           const loglens::SourceProfile& right) {
        return left.name < right.name;
    });
    sourceProfiles_ = std::move(next);
    rebuildSourceProfiles(utf8String(candidate.name));
    updateStatus(tr("Saved source profile '%1'").arg(utf8String(candidate.name)));
}

void MainWindow::applySavedQuery() {
    const QByteArray requestedName = savedQuery_->currentText().toUtf8();
    const auto selected = std::find_if(
        savedQueries_.begin(), savedQueries_.end(), [&](const loglens::SavedQuery& query) {
            return query.name == std::string(requestedName.constData(),
                                             static_cast<std::size_t>(requestedName.size()));
        });
    if (selected == savedQueries_.end()) {
        loglens::PersistenceError error;
        error.code = loglens::PersistenceErrorCode::InvalidValue;
        error.message = "select a saved query before applying it";
        showPersistenceError(tr("apply saved query"), error);
        return;
    }
    const QString expression = utf8String(selected->expression);
    filterEdit_->setText(expression);
    if (applyFilterText(expression,
                        tr("Applied saved query '%1'").arg(utf8String(selected->name)))) {
        const int index = static_cast<int>(selected - savedQueries_.begin());
        const QSignalBlocker blocker(savedQuery_);
        savedQuery_->setCurrentIndex(index);
    }
}

void MainWindow::saveSavedQuery() {
    const QByteArray nameBytes = savedQuery_->currentText().toUtf8();
    const QByteArray expressionBytes = filterEdit_->text().toUtf8();
    const loglens::SavedQuery candidate{
        std::string(nameBytes.constData(), static_cast<std::size_t>(nameBytes.size())),
        std::string(expressionBytes.constData(),
                    static_cast<std::size_t>(expressionBytes.size()))};
    if (candidate.name.empty()) {
        loglens::PersistenceError error;
        error.code = loglens::PersistenceErrorCode::InvalidValue;
        error.message = "query name must not be empty";
        showPersistenceError(tr("save saved query"), error);
        return;
    }
    std::vector<loglens::SavedQuery> next = savedQueries_;
    const auto existing = std::find_if(
        next.begin(), next.end(), [&](const loglens::SavedQuery& query) {
            return query.name == candidate.name;
        });
    if (existing == next.end()) {
        if (next.size() >= loglens::kMaxPersistedItems) {
            loglens::PersistenceError error;
            error.code = loglens::PersistenceErrorCode::LimitExceeded;
            error.message = "saved query count exceeds 128-item limit";
            showPersistenceError(tr("save saved query"), error);
            return;
        }
        next.push_back(candidate);
    } else {
        *existing = candidate;
    }
    if (!prepareDefaultStoreDirectory(false)) {
        return;
    }
    loglens::PersistenceError error;
    if (!loglens::saveSavedQueries(storePath(false).toStdString(), next, error)) {
        showPersistenceError(tr("save saved query"), error);
        return;
    }
    std::sort(next.begin(), next.end(), [](const loglens::SavedQuery& left,
                                           const loglens::SavedQuery& right) {
        return left.name < right.name;
    });
    savedQueries_ = std::move(next);
    rebuildSavedQueries(utf8String(candidate.name));
    updateStatus(tr("Saved query '%1'").arg(utf8String(candidate.name)));
}
