#ifndef WRAPPINGLAYOUT_H
#define WRAPPINGLAYOUT_H

#include <QLayout>
#include <QWidget>

class WrappingLayout : public QLayout
{
public:
    explicit WrappingLayout(QWidget *parent) : QLayout(parent)
    {
        setContentsMargins(0, 3, 0, 3);
        setSpacing(6);
    }

    ~WrappingLayout() override
    {
        while (QLayoutItem *item = takeAt(0))
            delete item;
    }

    void addItem(QLayoutItem *item) override { mItems.append(item); }
    int count() const override { return mItems.size(); }
    QLayoutItem *itemAt(int index) const override { return mItems.value(index); }
    QLayoutItem *takeAt(int index) override
    {
        return index >= 0 && index < mItems.size() ? mItems.takeAt(index) : nullptr;
    }
    Qt::Orientations expandingDirections() const override { return Qt::Horizontal; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override
    {
        return arrange(QRect(0, 0, width, 0), false);
    }
    QSize minimumSize() const override
    {
        QSize size;
        for (QLayoutItem *item : mItems) {
            if (!item->isEmpty())
                size = size.expandedTo(item->minimumSize());
        }
        const QMargins margins = contentsMargins();
        return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    }
    QSize sizeHint() const override { return minimumSize(); }
    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        arrange(rect, true);
    }

private:
    int arrange(const QRect &rect, bool apply) const
    {
        const QMargins margins = contentsMargins();
        const QRect area = rect.marginsRemoved(margins);
        int x = area.x();
        int y = area.y();
        int rowHeight = 0;
        for (QLayoutItem *item : mItems) {
            if (item->isEmpty())
                continue;
            const QSize size = item->sizeHint().expandedTo(item->minimumSize());
            if (x > area.x() && x + size.width() > area.right() + 1) {
                x = area.x();
                y += rowHeight + spacing();
                rowHeight = 0;
            }
            if (apply)
                item->setGeometry(QRect(QPoint(x, y), size));
            x += size.width() + spacing();
            rowHeight = qMax(rowHeight, size.height());
        }
        return y + rowHeight - rect.y() + margins.bottom();
    }

    QList<QLayoutItem *> mItems;
};

#endif
