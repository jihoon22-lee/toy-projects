#include "loglens/gui/highlight_delegate.hpp"

#include "loglens/gui/log_model.hpp"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QColor>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTextCharFormat>
#include <QTextLayout>
#include <QTextOption>
#include <QVector>

#include <algorithm>

namespace {

bool continuationAt(const std::string& value, std::size_t offset) {
    return offset < value.size()
           && (static_cast<unsigned char>(value[offset]) & 0xc0U) == 0x80U;
}

std::size_t threeByteLength(const std::string& value, std::size_t offset,
                            unsigned char first) {
    if (!continuationAt(value, offset + 1) || !continuationAt(value, offset + 2)) return 0;
    const unsigned char second = static_cast<unsigned char>(value[offset + 1]);
    if (first == 0xe0U) return second >= 0xa0U ? 3 : 0;
    if (first == 0xedU) return second <= 0x9fU ? 3 : 0;
    return 3;
}

std::size_t fourByteLength(const std::string& value, std::size_t offset,
                           unsigned char first) {
    if (!continuationAt(value, offset + 1) || !continuationAt(value, offset + 2)
        || !continuationAt(value, offset + 3)) return 0;
    const unsigned char second = static_cast<unsigned char>(value[offset + 1]);
    if (first == 0xf0U) return second >= 0x90U ? 4 : 0;
    if (first == 0xf4U) return second <= 0x8fU ? 4 : 0;
    return 4;
}

std::size_t utf8SequenceLength(const std::string& value, std::size_t offset) {
    const unsigned char first = static_cast<unsigned char>(value[offset]);
    if (first <= 0x7fU) return 1;
    if (first >= 0xc2U && first <= 0xdfU)
        return continuationAt(value, offset + 1) ? 2 : 0;
    if (first >= 0xe0U && first <= 0xefU) return threeByteLength(value, offset, first);
    if (first >= 0xf0U && first <= 0xf4U) return fourByteLength(value, offset, first);
    return 0;
}

bool validUtf8(const std::string& value) {
    for (std::size_t offset = 0; offset < value.size();) {
        const std::size_t length = utf8SequenceLength(value, offset);
        if (length == 0) return false;
        offset += length;
    }
    return true;
}

int utf16Position(const std::string& utf8, std::size_t byteOffset) {
    const std::size_t bounded = std::min(byteOffset, utf8.size());
    return QString::fromUtf8(utf8.data(), static_cast<int>(bounded)).size();
}

QColor readableForeground(const QColor& background) {
    return background.lightnessF() < 0.52 ? QColor(Qt::white) : QColor(Qt::black);
}

} // namespace

HighlightDelegate::HighlightDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void HighlightDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
    const auto* model = qobject_cast<const LogModel*>(index.model());
    const loglens::LogRecord* record = model == nullptr ? nullptr : model->recordAt(index.row());
    const std::vector<loglens::Span> spans =
        model == nullptr ? std::vector<loglens::Span>{} : model->highlightSpansAt(index.row());
    if (index.column() != LogModel::ColumnMessage || record == nullptr || spans.empty()) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }
    const std::string firstLine = record->message.substr(0, record->message.find('\n'));
    if (!validUtf8(firstLine)) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    QStyleOptionViewItem view(option);
    initStyleOption(&view, index);
    view.text.clear();
    const QStyle* style = view.widget == nullptr ? QApplication::style() : view.widget->style();
    style->drawControl(QStyle::CE_ItemViewItem, &view, painter, view.widget);
    const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &view,
                                                view.widget);

    const QString text = QString::fromUtf8(firstLine.data(), static_cast<int>(firstLine.size()));
    QTextLayout layout(text, view.font);
    QTextOption textOption;
    textOption.setWrapMode(QTextOption::NoWrap);
    layout.setTextOption(textOption);

    QVector<QTextLayout::FormatRange> formats;
    for (const loglens::Span& span : spans) {
        const std::size_t end = std::min(span.end, firstLine.size());
        if (span.begin >= end) continue;
        QTextLayout::FormatRange range;
        range.start = utf16Position(firstLine, span.begin);
        range.length = utf16Position(firstLine, end) - range.start;
        const QColor colour(QString::fromStdString(span.style));
        range.format.setBackground(colour);
        range.format.setForeground(readableForeground(colour));
        formats.push_back(range);
    }
    layout.setFormats(formats);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    if (line.isValid()) line.setLineWidth(textRect.width());
    layout.endLayout();

    painter->save();
    painter->setClipRect(textRect);
    const qreal top = textRect.top()
                      + (textRect.height() - layout.boundingRect().height()) / 2.0;
    layout.draw(painter, QPointF(textRect.left(), top));
    painter->restore();
}
