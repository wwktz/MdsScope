// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layout_dialog.hpp"

#include "shared.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QEasingCurve>
#include <QFormLayout>
#include <QHash>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

class PanelSetupDialogImpl final : public QDialog {
public:
    explicit PanelSetupDialogImpl(const PlotSpec& plot, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Panel Setup");
        auto* layout = new QFormLayout(this);
        title_ = new QLineEdit(plot.title, this);
        xLabel_ = new QLineEdit(plot.xLabel, this);
        yLabel_ = new QLineEdit(plot.yLabel, this);
        customX_ = new QCheckBox("Custom X range", this);
        customY_ = new QCheckBox("Custom Y range", this);
        xmin_ = new QLineEdit(this);
        xmax_ = new QLineEdit(this);
        ymin_ = new QLineEdit(this);
        ymax_ = new QLineEdit(this);
        grid_ = new QCheckBox("Show grid", this);
        grid_->setChecked(plot.grid);
        customX_->setChecked(plot.customXRange);
        customY_->setChecked(plot.customYRange);
        if (std::isfinite(plot.xmin)) xmin_->setText(QString::number(plot.xmin, 'g', 12));
        if (std::isfinite(plot.xmax)) xmax_->setText(QString::number(plot.xmax, 'g', 12));
        if (std::isfinite(plot.ymin)) ymin_->setText(QString::number(plot.ymin, 'g', 12));
        if (std::isfinite(plot.ymax)) ymax_->setText(QString::number(plot.ymax, 'g', 12));

        layout->addRow("Title", title_);
        layout->addRow("X label", xLabel_);
        layout->addRow("Y label", yLabel_);
        layout->addRow(grid_);
        layout->addRow(customX_);
        layout->addRow("X min", xmin_);
        layout->addRow("X max", xmax_);
        layout->addRow(customY_);
        layout->addRow("Y min", ymin_);
        layout->addRow("Y max", ymax_);

        auto* buttons = new QHBoxLayout;
        auto* ok = new QPushButton("OK", this);
        auto* cancel = new QPushButton("Cancel", this);
        buttons->addStretch(1);
        buttons->addWidget(ok);
        buttons->addWidget(cancel);
        layout->addRow(buttons);
        connect(ok, &QPushButton::clicked, this, [this] {
            if (!validateRanges()) {
                return;
            }
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    }

    void applyTo(PlotSpec* plot) const
    {
        plot->title = title_->text().trimmed();
        plot->xLabel = xLabel_->text().trimmed();
        plot->yLabel = yLabel_->text().trimmed();
        plot->grid = grid_->isChecked();
        plot->customXRange = customX_->isChecked();
        plot->customYRange = customY_->isChecked();
        optionalDoubleFromText(xmin_->text(), &plot->xmin);
        optionalDoubleFromText(xmax_->text(), &plot->xmax);
        optionalDoubleFromText(ymin_->text(), &plot->ymin);
        optionalDoubleFromText(ymax_->text(), &plot->ymax);
    }

private:
    bool validateRanges()
    {
        double value = qQNaN();
        for (QLineEdit* edit : {xmin_, xmax_, ymin_, ymax_}) {
            if (!optionalDoubleFromText(edit->text(), &value)) {
                QMessageBox::warning(this, "Panel Setup", "Range values must be numeric or empty.");
                return false;
            }
        }
        return true;
    }

    QLineEdit* title_ = nullptr;
    QLineEdit* xLabel_ = nullptr;
    QLineEdit* yLabel_ = nullptr;
    QCheckBox* customX_ = nullptr;
    QCheckBox* customY_ = nullptr;
    QCheckBox* grid_ = nullptr;
    QLineEdit* xmin_ = nullptr;
    QLineEdit* xmax_ = nullptr;
    QLineEdit* ymin_ = nullptr;
    QLineEdit* ymax_ = nullptr;
};

class LayoutCanvas::Impl final : public QWidget {
public:
    struct LayoutAnimationSnapshot {
        QHash<quint64, QRectF> items;
        QHash<quint64, QRectF> columns;
    };

    explicit Impl(const LayoutConfig& config, QWidget* parent = nullptr, bool editable = true)
        : QWidget(parent)
        , editable_(editable)
    {
        setMinimumSize(620, 420);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        layoutAnimation_ = new QVariantAnimation(this);
        layoutAnimation_->setDuration(180);
        layoutAnimation_->setEasingCurve(QEasingCurve::OutCubic);
        connect(layoutAnimation_, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& value) {
                    animationProgress_ = value.toReal();
                    update();
                });
        connect(layoutAnimation_, &QVariantAnimation::finished, this, [this] {
            animationProgress_ = 1.0;
            itemAnimationStarts_.clear();
            columnAnimationStarts_.clear();
            update();
        });
        for (int c = 0; c < config.columns.size(); ++c) {
            QVector<Item> col;
            for (int r = 0; r < config.columns[c].size(); ++r) {
                col.push_back(Item{c, r, false, false, nextItemId_++});
            }
            columns_.push_back(std::move(col));
            columnIds_.push_back(nextColumnId_++);
        }
        if (columns_.isEmpty()) {
            columns_.push_back({});
            columnIds_.push_back(nextColumnId_++);
        }
        initialColumns_ = columns_;
        initialColumnIds_ = columnIds_;
    }

    void createPendingPanelAtRight()
    {
        if (!editable_) {
            return;
        }
        const LayoutAnimationSnapshot before = captureVisualLayout();
        clearSelection();
        QVector<Item> col;
        col.push_back(Item{-1, -1, true, true, nextItemId_++});
        columns_.push_back(std::move(col));
        columnIds_.push_back(nextColumnId_++);
        draggingItem_ = false;
        draggedItemId_ = 0;
        lastDragTarget_ = {-1, -1};
        startLayoutAnimation(before);
        update();
    }

    void deleteSelected()
    {
        if (!editable_) {
            return;
        }
        const LayoutAnimationSnapshot before = captureVisualLayout();
        for (qsizetype c = columns_.size(); c-- > 0;) {
            for (qsizetype r = columns_[c].size(); r-- > 0;) {
                if (columns_[c][r].selected) {
                    columns_[c].removeAt(r);
                }
            }
            if (columns_[c].isEmpty() && columns_.size() > 1) {
                columns_.removeAt(c);
                columnIds_.removeAt(c);
            }
        }
        if (columns_.isEmpty()) {
            columns_.push_back({});
            columnIds_.push_back(nextColumnId_++);
        }
        draggingItem_ = false;
        draggingColumn_ = false;
        draggedItemId_ = 0;
        draggedColumn_ = -1;
        lastDragTarget_ = {-1, -1};
        startLayoutAnimation(before);
        update();
    }

    void reset()
    {
        const LayoutAnimationSnapshot before = captureVisualLayout();
        columns_ = initialColumns_;
        columnIds_ = initialColumnIds_;
        draggingItem_ = false;
        draggingColumn_ = false;
        draggedItemId_ = 0;
        draggedColumn_ = -1;
        dragMoved_ = false;
        lastDragTarget_ = {-1, -1};
        unsetCursor();
        startLayoutAnimation(before);
        update();
    }

    QVector<QVector<Item>> layoutItems() const
    {
        return columns_;
    }

    QVector<QPair<int, int>> selectedOriginalPanels() const
    {
        QVector<QPair<int, int>> panels;
        for (int c = 0; c < columns_.size(); ++c) {
            for (int r = 0; r < columns_[c].size(); ++r) {
                const Item& item = columns_[c][r];
                if (item.selected && !item.isNew && item.originalColumn >= 0 && item.originalRow >= 0) {
                    panels.push_back({item.originalColumn, item.originalRow});
                }
            }
        }
        return panels;
    }

    void selectAllOriginalPanels()
    {
        for (auto& col : columns_) {
            for (Item& item : col) {
                item.selected = !item.isNew;
            }
        }
        update();
    }

    void clearSelectedPanels()
    {
        clearSelection();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QPalette pal = palette();
        painter.fillRect(rect(), pal.color(QPalette::Base));
        painter.setPen(QPen(pal.color(QPalette::Mid), 1));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));

        const QRectF area = drawingArea();
        const int displayCols = std::max(1, static_cast<int>(columns_.size()));
        const int displayRows = std::max(1, maxRows());
        const double gap = 8.0;
        const double cellW = std::max(28.0, (area.width() - gap * (displayCols - 1)) / displayCols);
        const double cellH = std::max(22.0, (area.height() - gap * (displayRows - 1)) / displayRows);
        QFont headerFont = painter.font();
        headerFont.setBold(true);

        auto drawHeader = [&](const QRectF& rect,
                              const QString& label,
                              qreal opacity,
                              bool lifted) {
            if (lifted) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(0, 0, 0, 75));
                painter.drawRoundedRect(rect.translated(0, 4), 5, 5);
            }
            painter.save();
            painter.setOpacity(opacity);
            painter.setPen(Qt::NoPen);
            painter.setBrush(pal.color(QPalette::Button));
            painter.drawRoundedRect(rect, 5, 5);
            painter.setPen(pal.color(QPalette::ButtonText));
            painter.setFont(headerFont);
            painter.drawText(rect.adjusted(5, 0, -5, 0),
                             Qt::AlignCenter,
                             painter.fontMetrics().elidedText(
                                 label,
                                 Qt::ElideRight,
                                 std::max(
                                     1,
                                     static_cast<int>(rect.width() - 10))));
            painter.restore();
        };

        auto drawPanel = [&](const QRectF& rect,
                             const QColor& fill,
                             const QColor& text,
                             const QString& label,
                             qreal opacity,
                             bool lifted) {
            if (lifted) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(0, 0, 0, 75));
                painter.drawRoundedRect(rect.translated(0, 4), 7, 7);
            }
            painter.save();
            painter.setOpacity(opacity);
            drawCell(painter, rect, fill);
            painter.setPen(text);
            painter.setFont(headerFont);
            painter.drawText(rect.adjusted(5, 0, -5, 0),
                             Qt::AlignCenter,
                             painter.fontMetrics().elidedText(
                                 label,
                                 Qt::ElideRight,
                                 std::max(
                                     1,
                                     static_cast<int>(rect.width() - 10))));
            painter.restore();
        };

        struct FloatingPanel {
            QRectF rect;
            QColor fill;
            QColor text;
            QString label;
        };
        QVector<FloatingPanel> floatingPanels;
        QRectF floatingHeader;
        QString floatingHeaderLabel;
        bool hasFloatingHeader = false;
        qreal floatingColumnDelta = 0.0;
        const int floatingColumn =
            draggingColumn_ && dragMoved_ ? draggedColumn_ : -1;

        for (int c = 0; c < columns_.size(); ++c) {
            const QRectF targetHeader =
                columnHeaderRect(area, c, cellW, gap);
            const QRectF header = animatedRect(
                columnIds_.value(c), targetHeader, columnAnimationStarts_);
            const QString label = QString("Column %1").arg(c + 1);
            if (c == floatingColumn) {
                drawHeader(header, label, 0.18, false);
                floatingColumnDelta =
                    dragCurrentPos_.x() - dragGrabOffset_.x() - header.x();
                floatingHeader = header.translated(
                    floatingColumnDelta, 0);
                floatingHeaderLabel = label;
                hasFloatingHeader = true;
            } else {
                drawHeader(header, label, 1.0, false);
            }
        }

        for (int c = 0; c < columns_.size(); ++c) {
            for (int r = 0; r < columns_[c].size(); ++r) {
                const Item& item = columns_[c][r];
                const QColor fill = item.selected ? pal.color(QPalette::Highlight) : pal.color(QPalette::Midlight);
                const QRectF targetRect =
                    cellRect(area, c, r, cellW, cellH, gap);
                const QRectF rect = animatedRect(
                    item.id, targetRect, itemAnimationStarts_);
                const QColor text = item.selected
                    ? pal.color(QPalette::HighlightedText)
                    : pal.color(QPalette::Text);
                const QString label = item.isNew
                    ? QString("Panel %1 (new)").arg(item.id)
                    : QString("Panel %1").arg(item.id);
                const bool floatingItem =
                    draggingItem_ && dragMoved_
                    && item.id == draggedItemId_;
                const bool floatingColumnItem = c == floatingColumn;
                if (floatingItem || floatingColumnItem) {
                    drawPanel(rect, fill, text, label, 0.16, false);
                    QRectF lifted = rect;
                    if (floatingItem) {
                        lifted.moveTopLeft(
                            dragCurrentPos_ - dragGrabOffset_);
                        const qreal growX = lifted.width() * 0.015;
                        const qreal growY = lifted.height() * 0.015;
                        lifted.adjust(-growX, -growY, growX, growY);
                    } else {
                        lifted.translate(floatingColumnDelta, 0);
                    }
                    floatingPanels.push_back(
                        FloatingPanel{lifted, fill, text, label});
                } else {
                    drawPanel(rect, fill, text, label, 1.0, false);
                }
            }
        }

        if (editable_
            && !draggingColumn_
            && hoveredColumn_ >= 0
            && hoveredColumn_ < columns_.size()) {
            QRectF outline = animatedRect(
                columnIds_.value(hoveredColumn_),
                columnHeaderRect(area, hoveredColumn_, cellW, gap),
                columnAnimationStarts_);
            for (int r = 0; r < columns_[hoveredColumn_].size(); ++r) {
                const Item& item = columns_[hoveredColumn_][r];
                outline = outline.united(animatedRect(
                    item.id,
                    cellRect(area, hoveredColumn_, r, cellW, cellH, gap),
                    itemAnimationStarts_));
            }
            outline.adjust(-5.0, -5.0, 5.0, 5.0);
            QColor highlight = pal.color(QPalette::Highlight);
            QColor fill = highlight;
            fill.setAlpha(24);
            highlight.setAlpha(155);
            painter.save();
            painter.setBrush(fill);
            painter.setPen(QPen(highlight, 2.0));
            painter.drawRoundedRect(outline, 10.0, 10.0);
            painter.restore();
        }

        if (hasFloatingHeader) {
            drawHeader(
                floatingHeader, floatingHeaderLabel, 1.0, true);
        }
        for (const FloatingPanel& panel : std::as_const(floatingPanels)) {
            drawPanel(
                panel.rect,
                panel.fill,
                panel.text,
                panel.label,
                1.0,
                true);
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        setFocus(Qt::MouseFocusReason);
        const int header = editable_
            ? hitColumnHeader(event->position())
            : -1;
        if (header >= 0) {
            draggingColumn_ = true;
            draggingItem_ = false;
            draggedColumn_ = header;
            draggedItemId_ = 0;
            dragMoved_ = false;
            dragStart_ = event->position();
            dragCurrentPos_ = event->position();
            const auto targets = targetColumnRects();
            const quint64 columnId = columnIds_.value(header);
            const QRectF visualRect = animatedRect(
                columnId,
                targets.value(columnId),
                columnAnimationStarts_);
            dragGrabOffset_ =
                event->position() - visualRect.topLeft();
            lastDragTarget_ = {-1, -1};
            setCursor(Qt::ClosedHandCursor);
            return;
        }

        const auto hit = hitItem(event->position());
        if (hit.first < 0) {
            clearSelection();
            draggingItem_ = false;
            draggingColumn_ = false;
            draggedItemId_ = 0;
            draggedColumn_ = -1;
            update();
            return;
        }
        Item& item = columns_[hit.first][hit.second];
        if (!editable_) {
            item.selected = !item.selected;
            update();
            return;
        }
        pressWasSelected_ = item.selected;
        if (!item.selected) {
            item.selected = true;
        }
        draggingItem_ = true;
        draggingColumn_ = false;
        draggedItemId_ = item.id;
        draggedColumn_ = -1;
        dragMoved_ = false;
        dragStart_ = event->position();
        dragCurrentPos_ = event->position();
        const auto targets = targetItemRects();
        const QRectF visualRect = animatedRect(
            item.id,
            targets.value(item.id),
            itemAnimationStarts_);
        dragGrabOffset_ = event->position() - visualRect.topLeft();
        lastDragTarget_ = {-1, -1};
        setCursor(Qt::ClosedHandCursor);
        update();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!(event->buttons() & Qt::LeftButton)) {
            const int hovered = editable_
                ? hitColumnHeader(event->position())
                : -1;
            if (hoveredColumn_ != hovered) {
                hoveredColumn_ = hovered;
                update();
            }
            if (hoveredColumn_ >= 0) {
                setCursor(Qt::OpenHandCursor);
            } else {
                unsetCursor();
            }
            return;
        }
        if (!dragMoved_ && (event->position() - dragStart_).manhattanLength() < 3.0) {
            return;
        }
        dragMoved_ = true;
        dragCurrentPos_ = event->position();
        update();
        if (draggingColumn_) {
            moveDraggedColumnTo(event->position().x());
            return;
        }
        if (!draggingItem_) {
            return;
        }
        const QPair<int, int> target = targetForPosition(event->position());
        if (target == lastDragTarget_) {
            return;
        }
        lastDragTarget_ = target;
        moveDraggedItemTo(target);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        const bool settleItem =
            draggingItem_ && dragMoved_ && draggedItemId_ != 0;
        const bool settleColumn =
            draggingColumn_ && dragMoved_
            && draggedColumn_ >= 0
            && draggedColumn_ < columns_.size();
        LayoutAnimationSnapshot settleFrom;
        if (settleItem || settleColumn) {
            settleFrom = captureVisualLayout();
        }
        if (settleItem) {
            const auto targetIt = settleFrom.items.find(draggedItemId_);
            if (targetIt != settleFrom.items.end()) {
                QRectF lifted = targetIt.value();
                lifted.moveTopLeft(dragCurrentPos_ - dragGrabOffset_);
                const qreal growX = lifted.width() * 0.015;
                const qreal growY = lifted.height() * 0.015;
                lifted.adjust(-growX, -growY, growX, growY);
                targetIt.value() = lifted;
            }
        } else if (settleColumn) {
            const quint64 columnId = columnIds_[draggedColumn_];
            const auto headerIt = settleFrom.columns.find(columnId);
            if (headerIt != settleFrom.columns.end()) {
                const qreal delta =
                    dragCurrentPos_.x() - dragGrabOffset_.x()
                    - headerIt.value().x();
                headerIt.value().translate(delta, 0);
                for (const Item& item : columns_[draggedColumn_]) {
                    const auto itemIt = settleFrom.items.find(item.id);
                    if (itemIt != settleFrom.items.end()) {
                        itemIt.value().translate(delta, 0);
                    }
                }
            }
        }

        if (draggingItem_ && !dragMoved_ && pressWasSelected_) {
            if (Item* item = findItem(draggedItemId_)) {
                item->selected = false;
            }
        }
        draggingItem_ = false;
        draggingColumn_ = false;
        draggedItemId_ = 0;
        draggedColumn_ = -1;
        dragMoved_ = false;
        lastDragTarget_ = {-1, -1};
        hoveredColumn_ = editable_
            ? hitColumnHeader(event->position())
            : -1;
        if (hoveredColumn_ >= 0) {
            setCursor(Qt::OpenHandCursor);
        } else {
            unsetCursor();
        }
        if (settleItem || settleColumn) {
            startLayoutAnimation(settleFrom);
        }
        update();
    }

    void leaveEvent(QEvent* event) override
    {
        hoveredColumn_ = -1;
        if (!draggingItem_ && !draggingColumn_) {
            unsetCursor();
        }
        update();
        QWidget::leaveEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (editable_ && (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)) {
            deleteSelected();
            return;
        }
        QWidget::keyPressEvent(event);
    }

private:
    QHash<quint64, QRectF> targetItemRects() const
    {
        QHash<quint64, QRectF> targets;
        const QRectF area = drawingArea();
        const int displayCols =
            std::max(1, static_cast<int>(columns_.size()));
        const int displayRows = std::max(1, maxRows());
        const double gap = 8.0;
        const double cellW = std::max(
            28.0,
            (area.width() - gap * (displayCols - 1)) / displayCols);
        const double cellH = std::max(
            22.0,
            (area.height() - gap * (displayRows - 1)) / displayRows);
        for (int c = 0; c < columns_.size(); ++c) {
            for (int r = 0; r < columns_[c].size(); ++r) {
                targets.insert(
                    columns_[c][r].id,
                    cellRect(area, c, r, cellW, cellH, gap));
            }
        }
        return targets;
    }

    QHash<quint64, QRectF> targetColumnRects() const
    {
        QHash<quint64, QRectF> targets;
        const QRectF area = drawingArea();
        const int displayCols =
            std::max(1, static_cast<int>(columns_.size()));
        const double gap = 8.0;
        const double cellW = std::max(
            28.0,
            (area.width() - gap * (displayCols - 1)) / displayCols);
        for (int c = 0;
             c < columns_.size() && c < columnIds_.size();
             ++c) {
            targets.insert(
                columnIds_[c],
                columnHeaderRect(area, c, cellW, gap));
        }
        return targets;
    }

    QRectF animatedRect(
        quint64 id,
        const QRectF& target,
        const QHash<quint64, QRectF>& starts) const
    {
        const auto it = starts.constFind(id);
        if (it == starts.cend() || animationProgress_ >= 1.0) {
            return target;
        }
        const QRectF& start = it.value();
        const qreal t = animationProgress_;
        return QRectF(
            start.x() + (target.x() - start.x()) * t,
            start.y() + (target.y() - start.y()) * t,
            start.width() + (target.width() - start.width()) * t,
            start.height() + (target.height() - start.height()) * t);
    }

    LayoutAnimationSnapshot captureVisualLayout() const
    {
        LayoutAnimationSnapshot snapshot;
        const auto itemTargets = targetItemRects();
        for (auto it = itemTargets.cbegin(); it != itemTargets.cend(); ++it) {
            snapshot.items.insert(
                it.key(),
                animatedRect(it.key(), it.value(), itemAnimationStarts_));
        }
        const auto columnTargets = targetColumnRects();
        for (auto it = columnTargets.cbegin();
             it != columnTargets.cend();
             ++it) {
            snapshot.columns.insert(
                it.key(),
                animatedRect(
                    it.key(), it.value(), columnAnimationStarts_));
        }
        return snapshot;
    }

    void startLayoutAnimation(const LayoutAnimationSnapshot& before)
    {
        layoutAnimation_->stop();
        itemAnimationStarts_.clear();
        columnAnimationStarts_.clear();

        const auto itemTargets = targetItemRects();
        for (auto it = itemTargets.cbegin(); it != itemTargets.cend(); ++it) {
            const QRectF start = before.items.value(
                it.key(),
                QRectF(it.value().center(), QSizeF()));
            if (start != it.value()) {
                itemAnimationStarts_.insert(it.key(), start);
            }
        }

        const auto columnTargets = targetColumnRects();
        for (auto it = columnTargets.cbegin();
             it != columnTargets.cend();
             ++it) {
            const QRectF start = before.columns.value(
                it.key(),
                QRectF(it.value().center(), QSizeF()));
            if (start != it.value()) {
                columnAnimationStarts_.insert(it.key(), start);
            }
        }

        if (itemAnimationStarts_.isEmpty()
            && columnAnimationStarts_.isEmpty()) {
            animationProgress_ = 1.0;
            return;
        }
        animationProgress_ = 0.0;
        layoutAnimation_->setStartValue(0.0);
        layoutAnimation_->setEndValue(1.0);
        layoutAnimation_->start();
    }

    QRectF drawingArea() const
    {
        return rect().adjusted(18, 52, -18, -18);
    }

    int maxRows() const
    {
        int rows = 1;
        for (const auto& col : columns_) {
            rows = std::max(rows, static_cast<int>(col.size()));
        }
        return rows;
    }

    void clearSelection()
    {
        for (auto& col : columns_) {
            for (Item& item : col) {
                item.selected = false;
            }
        }
    }

    QPair<int, int> hitItem(const QPointF& pos) const
    {
        const QRectF area = drawingArea();
        const int displayCols = std::max(1, static_cast<int>(columns_.size()));
        const int displayRows = std::max(1, maxRows());
        const double gap = 8.0;
        const double cellW = std::max(28.0, (area.width() - gap * (displayCols - 1)) / displayCols);
        const double cellH = std::max(22.0, (area.height() - gap * (displayRows - 1)) / displayRows);
        for (int c = 0; c < columns_.size(); ++c) {
            for (int r = 0; r < columns_[c].size(); ++r) {
                if (cellRect(area, c, r, cellW, cellH, gap).contains(pos)) {
                    return {c, r};
                }
            }
        }
        return {-1, -1};
    }

    int hitColumnHeader(const QPointF& pos) const
    {
        const QRectF area = drawingArea();
        const int displayCols = std::max(1, static_cast<int>(columns_.size()));
        const double gap = 8.0;
        const double cellW = std::max(
            28.0,
            (area.width() - gap * (displayCols - 1)) / displayCols);
        for (int c = 0; c < columns_.size(); ++c) {
            if (columnHeaderRect(area, c, cellW, gap).contains(pos)) {
                return c;
            }
        }
        return -1;
    }

    QPair<int, int> targetForPosition(const QPointF& pos) const
    {
        const QRectF area = drawingArea();
        const int displayCols = std::max(1, static_cast<int>(columns_.size()));
        const int displayRows = std::max(1, maxRows());
        const double gap = 8.0;
        const double cellW = std::max(28.0, (area.width() - gap * (displayCols - 1)) / displayCols);
        const double cellH = std::max(22.0, (area.height() - gap * (displayRows - 1)) / displayRows);
        const double boundaryBand = std::min(30.0, std::max(12.0, cellW * 0.16));

        if (pos.x() <= area.left() + boundaryBand) {
            return {0, -1};
        }
        for (int c = 0; c < columns_.size() - 1; ++c) {
            const double boundary = area.left() + c * (cellW + gap) + cellW + gap * 0.5;
            if (std::abs(pos.x() - boundary) <= boundaryBand) {
                return {c + 1, -1};
            }
        }
        const double afterLast =
            area.left() + static_cast<double>(columns_.size() - 1) * (cellW + gap) + cellW;
        if (pos.x() >= afterLast - boundaryBand) {
            return {static_cast<int>(columns_.size()), -1};
        }

        for (int c = 0; c < columns_.size(); ++c) {
            const QRectF columnRect(area.left() + c * (cellW + gap), area.top(), cellW, area.height());
            if (columnRect.adjusted(boundaryBand * 0.25, 0, -boundaryBand * 0.25, 0).contains(pos)) {
                const int row = std::clamp(static_cast<int>((pos.y() - area.top()) / (cellH + gap) + 0.5),
                                           0,
                                           static_cast<int>(columns_[c].size()));
                return {c, row};
            }
        }
        int newColumn = 0;
        for (int c = 0; c < columns_.size(); ++c) {
            const double centerX = area.left() + c * (cellW + gap) + cellW * 0.5;
            if (pos.x() > centerX) {
                newColumn = c + 1;
            }
        }
        return {std::clamp(newColumn, 0, static_cast<int>(columns_.size())), -1};
    }

    Item* findItem(quint64 id)
    {
        for (int c = 0; c < columns_.size(); ++c) {
            for (int r = 0; r < columns_[c].size(); ++r) {
                if (columns_[c][r].id == id) {
                    return &columns_[c][r];
                }
            }
        }
        return nullptr;
    }

    QPair<int, int> findItemPosition(quint64 id) const
    {
        for (int c = 0; c < columns_.size(); ++c) {
            for (int r = 0; r < columns_[c].size(); ++r) {
                if (columns_[c][r].id == id) {
                    return {c, r};
                }
            }
        }
        return {-1, -1};
    }

    void moveDraggedItemTo(QPair<int, int> target)
    {
        const QPair<int, int> source = findItemPosition(draggedItemId_);
        const int sourceCol = source.first;
        const int sourceRow = source.second;
        if (sourceCol < 0 || sourceRow < 0) {
            return;
        }
        if (target.second >= 0
            && target.first == sourceCol
            && (target.second == sourceRow || target.second == sourceRow + 1)) {
            return;
        }

        const LayoutAnimationSnapshot before = captureVisualLayout();
        Item item = columns_[sourceCol].takeAt(sourceRow);
        const bool removedSourceColumn =
            columns_[sourceCol].isEmpty() && columns_.size() > 1;
        quint64 removedColumnId = 0;
        if (removedSourceColumn) {
            removedColumnId = columnIds_[sourceCol];
            columns_.removeAt(sourceCol);
            columnIds_.removeAt(sourceCol);
            if (target.first > sourceCol) {
                --target.first;
            }
        } else if (target.second >= 0
                   && target.first == sourceCol
                   && target.second > sourceRow) {
            --target.second;
        }

        if (target.second < 0) {
            target.first = std::clamp(target.first, 0, static_cast<int>(columns_.size()));
            columns_.insert(target.first, QVector<Item>{item});
            columnIds_.insert(
                target.first,
                removedColumnId != 0 ? removedColumnId : nextColumnId_++);
        } else {
            target.first = std::clamp(target.first, 0, std::max(0, static_cast<int>(columns_.size()) - 1));
            target.second = std::clamp(target.second, 0, static_cast<int>(columns_[target.first].size()));
            columns_[target.first].insert(target.second, item);
        }
        startLayoutAnimation(before);
        update();
    }

    void moveDraggedColumnTo(qreal x)
    {
        if (draggedColumn_ < 0
            || draggedColumn_ >= columns_.size()
            || columns_.size() < 2) {
            return;
        }
        const QRectF area = drawingArea();
        const double gap = 8.0;
        const double cellW = std::max(
            28.0,
            (area.width() - gap * static_cast<double>(columns_.size() - 1))
                / static_cast<double>(columns_.size()));
        const double stride = cellW + gap;
        const int target = std::clamp(
            qRound((x - area.left() - cellW * 0.5) / stride),
            0,
            static_cast<int>(columns_.size()) - 1);
        if (target == draggedColumn_) {
            return;
        }
        const LayoutAnimationSnapshot before = captureVisualLayout();
        QVector<Item> column = columns_.takeAt(draggedColumn_);
        const quint64 columnId = columnIds_.takeAt(draggedColumn_);
        columns_.insert(target, std::move(column));
        columnIds_.insert(target, columnId);
        draggedColumn_ = target;
        startLayoutAnimation(before);
        update();
    }

    static QRectF cellRect(const QRectF& area, int column, int row, double cellW, double cellH, double gap)
    {
        return QRectF(area.left() + column * (cellW + gap), area.top() + row * (cellH + gap), cellW, cellH);
    }

    static QRectF columnHeaderRect(
        const QRectF& area, int column, double cellW, double gap)
    {
        return QRectF(
            area.left() + column * (cellW + gap),
            area.top() - 34.0,
            cellW,
            26.0);
    }

    static void drawCell(QPainter& painter, const QRectF& rect, const QColor& color)
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRoundedRect(rect, 7, 7);
    }

    QVector<QVector<Item>> columns_;
    QVector<QVector<Item>> initialColumns_;
    QVector<quint64> columnIds_;
    QVector<quint64> initialColumnIds_;
    QHash<quint64, QRectF> itemAnimationStarts_;
    QHash<quint64, QRectF> columnAnimationStarts_;
    QVariantAnimation* layoutAnimation_ = nullptr;
    quint64 nextItemId_ = 1;
    quint64 nextColumnId_ = 1;
    qreal animationProgress_ = 1.0;
    bool editable_ = true;
    bool draggingItem_ = false;
    bool draggingColumn_ = false;
    bool dragMoved_ = false;
    bool pressWasSelected_ = false;
    quint64 draggedItemId_ = 0;
    int draggedColumn_ = -1;
    int hoveredColumn_ = -1;
    QPointF dragStart_;
    QPointF dragCurrentPos_;
    QPointF dragGrabOffset_;
    QPair<int, int> lastDragTarget_ = {-1, -1};
};

class LayoutSetupDialogImpl final : public QDialog {
public:
    explicit LayoutSetupDialogImpl(const LayoutConfig& config, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Layout Setup");
        resize(900, 560);
        auto* mainLayout = new QHBoxLayout(this);
        canvas_ = new LayoutCanvas(config, this);
        mainLayout->addWidget(canvas_, 1);

        auto* side = new QWidget(this);
        auto* sideLayout = new QVBoxLayout(side);
        auto* addPanelButton = new QPushButton("Add new panel", side);
        auto* deleteButton = new QPushButton("Delete selected", side);
        auto* resetButton = new QPushButton("Reset", side);
        auto* applyButton = new QPushButton("Apply", side);
        auto* cancelButton = new QPushButton("Cancel", side);
        sideLayout->addWidget(addPanelButton);
        sideLayout->addSpacing(10);
        sideLayout->addWidget(deleteButton);
        sideLayout->addWidget(resetButton);
        sideLayout->addStretch(1);
        sideLayout->addWidget(applyButton);
        sideLayout->addWidget(cancelButton);
        mainLayout->addWidget(side);

        connect(addPanelButton, &QPushButton::clicked, canvas_, &LayoutCanvas::createPendingPanelAtRight);
        connect(deleteButton, &QPushButton::clicked, canvas_, &LayoutCanvas::deleteSelected);
        connect(resetButton, &QPushButton::clicked, canvas_, &LayoutCanvas::reset);
        connect(applyButton, &QPushButton::clicked, this, &QDialog::accept);
        connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    }

    QVector<QVector<LayoutCanvas::Item>> layoutItems() const { return canvas_->layoutItems(); }

private:
    LayoutCanvas* canvas_ = nullptr;
};

LayoutCanvas::LayoutCanvas(const LayoutConfig& config,
                           QWidget* parent,
                           bool editable)
    : QWidget(parent)
    , impl_(std::make_unique<Impl>(config, this, editable))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(impl_.get());
}

LayoutCanvas::~LayoutCanvas() = default;

void LayoutCanvas::createPendingPanelAtRight()
{
    impl_->createPendingPanelAtRight();
}

void LayoutCanvas::deleteSelected()
{
    impl_->deleteSelected();
}

void LayoutCanvas::reset()
{
    impl_->reset();
}

QVector<QVector<LayoutCanvas::Item>> LayoutCanvas::layoutItems() const
{
    return impl_->layoutItems();
}

QVector<QPair<int, int>> LayoutCanvas::selectedOriginalPanels() const
{
    return impl_->selectedOriginalPanels();
}

void LayoutCanvas::selectAllOriginalPanels()
{
    impl_->selectAllOriginalPanels();
}

void LayoutCanvas::clearSelectedPanels()
{
    impl_->clearSelectedPanels();
}

std::optional<PlotSpec> editPanelSetup(const PlotSpec& plot,
                                       QWidget* parent)
{
    PanelSetupDialogImpl dialog(plot, parent);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    PlotSpec edited = plot;
    dialog.applyTo(&edited);
    return edited;
}

std::optional<QVector<QVector<LayoutCanvas::Item>>> editLayoutSetup(
    const LayoutConfig& config,
    QWidget* parent)
{
    LayoutSetupDialogImpl dialog(config, parent);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    return dialog.layoutItems();
}

bool layoutItemsMatchConfig(
    const QVector<QVector<LayoutCanvas::Item>>& layout,
    const LayoutConfig& config)
{
    if (layout.size() != config.columns.size()) {
        return false;
    }
    for (int c = 0; c < layout.size(); ++c) {
        if (layout[c].size() != config.columns[c].size()) {
            return false;
        }
        for (int r = 0; r < layout[c].size(); ++r) {
            const LayoutCanvas::Item& item = layout[c][r];
            if (item.isNew || item.originalColumn != c || item.originalRow != r) {
                return false;
            }
        }
    }
    return true;
}
