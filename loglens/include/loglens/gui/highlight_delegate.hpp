#pragma once

#include <QStyledItemDelegate>

// Renders the byte-ranged core highlight spans in the message column. The
// delegate deliberately owns no rules: LogModel remains the single source of
// truth for the filtered row and its compiled triage state.
class HighlightDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit HighlightDelegate(QObject* parent = nullptr);
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
};
