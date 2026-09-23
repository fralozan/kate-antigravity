#include "chatcompletionpopup.h"

#include <QVBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QKeyEvent>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>
#include <QScreen>

namespace {
class CompletionItemDelegate : public QStyledItemDelegate
{
public:
    explicit CompletionItemDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        Q_UNUSED(option);
        Q_UNUSED(index);
        return QSize(260, 32);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();

        const bool isSelected = option.state & QStyle::State_Selected;
        if (isSelected) {
            painter->fillRect(option.rect, option.palette.highlight());
        } else {
            painter->fillRect(option.rect, option.palette.base());
        }

        const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
        const QString text = index.data(Qt::UserRole + 1).toString();
        const QString hint = index.data(Qt::UserRole + 2).toString();

        const int padding = 6;
        QRect iconRect(option.rect.left() + padding, option.rect.top() + (option.rect.height() - 16) / 2, 16, 16);
        if (!icon.isNull()) {
            icon.paint(painter, iconRect);
        }

        painter->setRenderHint(QPainter::Antialiasing, true);

        // Primary text
        QFont titleFont = option.font;
        titleFont.setBold(true);
        painter->setFont(titleFont);
        painter->setPen(isSelected ? option.palette.highlightedText().color() : option.palette.text().color());

        QRect textRect(option.rect.left() + 28, option.rect.top(), option.rect.width() - 36, option.rect.height());
        QFontMetrics fm(titleFont);
        int textWidth = fm.horizontalAdvance(text);

        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);

        // Hint text
        if (!hint.isEmpty()) {
            QFont hintFont = option.font;
            hintFont.setPointSize(qMax(8, option.font.pointSize() - 1));
            painter->setFont(hintFont);
            painter->setPen(isSelected ? option.palette.highlightedText().color() : option.palette.placeholderText().color());

            QRect hintRect(textRect.left() + textWidth + 8, textRect.top(), textRect.width() - textWidth - 12, textRect.height());
            QFontMetrics hintFm(hintFont);
            QString elidedHint = hintFm.elidedText(hint, Qt::ElideRight, hintRect.width());
            painter->drawText(hintRect, Qt::AlignLeft | Qt::AlignVCenter, elidedHint);
        }

        painter->restore();
    }
};
} // namespace

ChatCompletionPopup::ChatCompletionPopup(QWidget *parent)
    : QFrame(parent, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Plain);

    setStyleSheet(QStringLiteral(
        "ChatCompletionPopup {"
        "  background-color: palette(base);"
        "  border: 1px solid palette(mid);"
        "  border-radius: 6px;"
        "}"
        "QListWidget {"
        "  background-color: transparent;"
        "  border: none;"
        "}"
    ));

    setupUi();
}

void ChatCompletionPopup::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(0);

    m_listWidget = new QListWidget(this);
    m_listWidget->setFocusPolicy(Qt::NoFocus);
    m_listWidget->setItemDelegate(new CompletionItemDelegate(this));
    m_listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    connect(m_listWidget, &QListWidget::itemClicked, this, &ChatCompletionPopup::onItemDoubleClicked);
    connect(m_listWidget, &QListWidget::itemDoubleClicked, this, &ChatCompletionPopup::onItemDoubleClicked);

    layout->addWidget(m_listWidget);
}

void ChatCompletionPopup::setCompletions(const QList<CompletionItem> &items)
{
    m_items = items;
    m_listWidget->clear();

    for (const auto &item : items) {
        auto *listItem = new QListWidgetItem(m_listWidget);
        listItem->setIcon(item.icon);
        listItem->setData(Qt::UserRole + 1, item.text);
        listItem->setData(Qt::UserRole + 2, item.hint);
        m_listWidget->addItem(listItem);
    }

    if (!items.isEmpty()) {
        m_listWidget->setCurrentRow(0);
    }
}

bool ChatCompletionPopup::hasItems() const
{
    return !m_items.isEmpty();
}

void ChatCompletionPopup::showAt(const QPoint &globalPos)
{
    if (m_items.isEmpty()) {
        hide();
        return;
    }

    const int itemHeight = 32;
    const int visibleCount = qMin(m_items.size(), 7);
    const int popupHeight = visibleCount * itemHeight + 6;
    const int popupWidth = qMax(280, width());

    resize(popupWidth, popupHeight);

    // Screen boundary check
    QRect screenRect = QGuiApplication::primaryScreen()->availableGeometry();
    QPoint targetPos = globalPos;

    if (targetPos.y() + popupHeight > screenRect.bottom()) {
        // Show above cursor
        targetPos.setY(globalPos.y() - popupHeight - 20);
    }

    if (targetPos.x() + popupWidth > screenRect.right()) {
        targetPos.setX(screenRect.right() - popupWidth - 4);
    }

    move(targetPos);
    show();
    raise();
}

bool ChatCompletionPopup::handleKeyPress(QKeyEvent *event)
{
    if (!isVisible()) {
        return false;
    }

    switch (event->key()) {
    case Qt::Key_Up:
        selectPrevious();
        return true;
    case Qt::Key_Down:
        selectNext();
        return true;
    case Qt::Key_Tab:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        acceptCurrent();
        return true;
    case Qt::Key_Escape:
        hide();
        Q_EMIT dismissed();
        return true;
    default:
        break;
    }

    return false;
}

void ChatCompletionPopup::selectNext()
{
    int row = m_listWidget->currentRow();
    if (row < m_listWidget->count() - 1) {
        m_listWidget->setCurrentRow(row + 1);
    } else {
        m_listWidget->setCurrentRow(0);
    }
}

void ChatCompletionPopup::selectPrevious()
{
    int row = m_listWidget->currentRow();
    if (row > 0) {
        m_listWidget->setCurrentRow(row - 1);
    } else {
        m_listWidget->setCurrentRow(m_listWidget->count() - 1);
    }
}

void ChatCompletionPopup::acceptCurrent()
{
    const int row = m_listWidget->currentRow();
    if (row >= 0 && row < m_items.size()) {
        const auto item = m_items.at(row);
        hide();
        Q_EMIT itemSelected(item);
    } else {
        hide();
    }
}

void ChatCompletionPopup::onItemDoubleClicked(QListWidgetItem *item)
{
    int row = m_listWidget->row(item);
    if (row >= 0 && row < m_items.size()) {
        const auto chosen = m_items.at(row);
        hide();
        Q_EMIT itemSelected(chosen);
    }
}

bool ChatCompletionPopup::eventFilter(QObject *watched, QEvent *event)
{
    return QFrame::eventFilter(watched, event);
}
