#include "loglens/gui/main_window.hpp"

#include <QByteArray>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QTableView>
#include <QHeaderView>
#include <QTabWidget>
#include <QPushButton>
#include "loglens/gui/log_model.hpp"
#include "loglens/evidence.hpp"
#include <QDir>
#include <QFileDialog>
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

QString MainWindow::suggestedSessionPath() const {
    // Never the log itself: a confirmed overwrite would replace the evidence
    // under investigation with session JSON.
    const QFileInfo log(currentPath_);
    return log.absoluteDir().filePath(log.completeBaseName() +
                                      QStringLiteral(".session.json"));
}

void MainWindow::saveSessionToFile() {
    if (currentPath_.isEmpty()) {
        updateStatus(tr("Open a log before saving a session"));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save session"), suggestedSessionPath(),
        tr("LogLens sessions (*.session.json);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    saveSessionTo(path);
}

bool MainWindow::saveSessionTo(const QString& path) {
    if (currentPath_.isEmpty()) {
        updateStatus(tr("Open a log before saving a session"));
        return false;
    }
    if (QFileInfo(path).absoluteFilePath() == QFileInfo(currentPath_).absoluteFilePath()) {
        updateStatus(tr("Refusing to save the session over the open log"));
        return false;
    }
    loglens::SessionState state;
    const QByteArray nameBytes = QFileInfo(path).completeBaseName().toUtf8();
    state.name = std::string(nameBytes.constData(),
                             static_cast<std::size_t>(nameBytes.size()));
    const QByteArray pathBytes = currentPath_.toUtf8();
    state.source_path =
        std::string(pathBytes.constData(), static_cast<std::size_t>(pathBytes.size()));
    state.format = selectedFormat();
    state.multiline = selectedMultilinePolicy();
    state.max_record_bytes = static_cast<std::size_t>(maxRecordBytes_->value());
    state.format_plugin = pluginPath_.toStdString();
    state.search = searchEdit_->text().toStdString();
    state.whole_file_search = wholeSearchEdit_->text().toStdString();
    state.investigation_tab =
        static_cast<std::size_t>(findChild<QTabWidget *>("investigationTabs")->currentIndex());
    state.settings_open = findChild<QPushButton *>("sourceSettingsButton")->isChecked();
    state.follow = followBox_->isChecked();
    state.tail_mode = selectedLoadMode() == loglens::InitialLoadMode::TailRecords;
    state.tail_records = static_cast<std::size_t>(tailRecords_->value());
    state.selected_window = selectedWindow_;
    state.baseline_window = baselineWindow_;
    state.comparison_window = comparisonWindow_;
    state.triage = triageState_;
    state.layout = saveState(2).toBase64().toStdString();
    state.geometry = saveGeometry().toBase64().toStdString();
    state.table_header = table_->horizontalHeader()->saveState().toBase64().toStdString();
    const auto evidence = loglens::captureSourceEvidence(state.source_path);
    if (!evidence.ok()) {
        updateStatus(tr("Cannot verify source before saving: %1")
                         .arg(QString::fromStdString(evidence.error)));
        return false;
    }
    state.source_identity = evidence.identity;
    state.source_modified = evidence.modified;
    state.source_fingerprint = evidence.fingerprint;
    state.fingerprint_bytes = evidence.fingerprint_bytes;
    state.source_size = evidence.size;
    state.source_generation = model_->generation();
    if (!pluginPath_.isEmpty()) {
        const auto plugin = loglens::captureSourceEvidence(state.format_plugin, 4 * 1024 * 1024);
        if (!plugin.ok() || plugin.fingerprint_bytes != plugin.size) {
            updateStatus(tr("Cannot fingerprint parser plugin"));
            return false;
        }
        if (!formatPlugin_ || plugin.fingerprint != formatPlugin_->document_fingerprint) {
            updateStatus(tr("Parser plugin changed after loading; reload it before saving the investigation"));
            return false;
        }
        state.plugin_fingerprint = formatPlugin_->document_fingerprint;
    }
    const QByteArray filterBytes = activeFilterText_.toUtf8();
    state.filter = std::string(filterBytes.constData(),
                               static_cast<std::size_t>(filterBytes.size()));
    const QByteArray targetBytes = path.toUtf8();
    loglens::PersistenceError error;
    if (!loglens::saveSession(
            std::string(targetBytes.constData(), static_cast<std::size_t>(targetBytes.size())),
            state, error)) {
        showPersistenceError(tr("save session"), error);
        return false;
    }
    updateStatus(tr("Session saved to %1").arg(QFileInfo(path).fileName()));
    return true;
}

void MainWindow::openSessionFile() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open session"), QString(),
        tr("LogLens sessions (*.session.json);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    openSession(path);
}

bool MainWindow::openSession(const QString& path) {
    const QByteArray pathBytes = path.toUtf8();
    const loglens::SessionLoadResult result = loglens::loadSession(
        std::string(pathBytes.constData(), static_cast<std::size_t>(pathBytes.size())));
    if (!result.ok()) {
        showPersistenceError(tr("load session"), result.error);
        return false;
    }
    if (!result.found) {
        updateStatus(tr("Session file not found"));
        return false;
    }
    loglens::SessionState state = result.state;
    // Validate the parser before changing the active investigation.
    if (!setFormatPluginPath(utf8String(state.format_plugin)))
        return false;
    QString evidenceNotice;
    const auto evidence = loglens::captureSourceEvidence(state.source_path);
    const bool sameSource = evidence.ok() && !state.source_fingerprint.empty() &&
                            evidence.identity == state.source_identity &&
                            evidence.size == state.source_size &&
                            evidence.modified == state.source_modified &&
                            evidence.fingerprint == state.source_fingerprint &&
                            evidence.fingerprint_bytes == state.fingerprint_bytes;
    if (!sameSource)
        evidenceNotice =
            tr("Source changed or lacks a saved fingerprint; unmatched notes remain archived");
    if (!state.format_plugin.empty() && !state.plugin_fingerprint.empty()) {
        const auto plugin = loglens::captureSourceEvidence(state.format_plugin, 4 * 1024 * 1024);
        if (!plugin.ok() || plugin.fingerprint != state.plugin_fingerprint)
            evidenceNotice += tr(" · Parser plugin changed");
    }
    if (sameSource) {
        for (auto &entry : state.triage.entries) {
            if (entry.source_identity == state.source_identity &&
                entry.generation == state.source_generation)
                entry.generation = 0;
        }
    }
    // A session owns a complete investigation state; legacy files carry an empty triage state.
    triageState_ = state.triage;
    rebuildHighlightRules();
    setProfileControls(loglens::SourceProfile{state.name, state.format, state.multiline,
                                            state.max_record_bytes});
    // The CLI applies --level and --filter as two expressions that must both
    // hold. AND binds tighter than OR, so the saved filter is parenthesised
    // to keep that meaning once the two are joined into one expression.
    QString filter = utf8String(state.filter);
    if (!state.level.empty()) {
        const QString levelExpr =
            QStringLiteral("level>=") + utf8String(state.level);
        filter = filter.isEmpty()
                     ? levelExpr
                     : levelExpr + QStringLiteral(" AND (") + filter + QStringLiteral(")");
    }
    // Applied even when empty: a session without a filter clears the one
    // left over from the previous investigation.
    filterEdit_->setText(filter);
    applyFilterText(filter);
    if (state.source_path.empty()) {
        updateStatus(tr("Session '%1' loaded without a source path")
                         .arg(utf8String(state.name)));
        return true;
    }
    followBox_->setChecked(state.follow);
    loadMode_->setCurrentIndex(state.tail_mode ? 0 : 1);
    tailRecords_->setValue(static_cast<int>(std::min(state.tail_records, record_capacity_)));
    searchEdit_->setText(utf8String(state.search));
    wholeSearchEdit_->setText(utf8String(state.whole_file_search));
    findChild<QTabWidget *>("investigationTabs")
        ->setCurrentIndex(static_cast<int>(state.investigation_tab));
    findChild<QPushButton *>("sourceSettingsButton")->setChecked(state.settings_open);
    openPath(utf8String(state.source_path));
    pendingSession_ = state;
    sessionEvidenceNotice_ = evidenceNotice;
    if (!state.layout.empty())
        restoreState(QByteArray::fromBase64(QByteArray::fromStdString(state.layout)), 2);
    if (!state.geometry.empty())
        restoreGeometry(QByteArray::fromBase64(QByteArray::fromStdString(state.geometry)));
    if (!state.table_header.empty())
        table_->horizontalHeader()->restoreState(
            QByteArray::fromBase64(QByteArray::fromStdString(state.table_header)));
    return true;
}

bool MainWindow::setFormatPluginPath(const QString &path) {
    if (path.isEmpty()) {
        formatPlugin_.reset();
        pluginPath_.clear();
        pluginLabel_->setText(tr("Built-in parser"));
        return true;
    }
    auto plugin = std::make_shared<loglens::FormatPlugin>();
    std::string error;
    if (loglens::loadFormatPlugin(path.toStdString(), *plugin, error) !=
        loglens::FormatPluginError::None) {
        updateStatus(tr("Cannot load parser plugin: %1").arg(QString::fromStdString(error)));
        return false;
    }
    pluginPath_ = QFileInfo(path).absoluteFilePath();
    pluginLabel_->setText(tr("Plugin: %1").arg(QString::fromStdString(plugin->name)));
    formatPlugin_ = std::move(plugin);
    return true;
}
