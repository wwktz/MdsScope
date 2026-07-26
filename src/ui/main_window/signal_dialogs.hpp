// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "shared.hpp"

class SignalDialog final : public QDialog {
public:
    explicit SignalDialog(const PlotSpec& base, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Signal");
        auto* layout = new QFormLayout(this);
        shot_ = new QLineEdit(base.shot, this);
        y_ = new QLineEdit(this);
        x_ = new QLineEdit(this);
        experiment_ = new QLineEdit(this);
        server_ = new QLineEdit(this);
        if (!base.signalSpecs.isEmpty()) {
            experiment_->setText(base.signalSpecs.front().experiment);
            server_->setText(base.signalSpecs.front().serverIp);
        }
        layout->addRow("Shot", shot_);
        layout->addRow("Y expr", y_);
        layout->addRow("X expr", x_);
        layout->addRow("Tree", experiment_);
        layout->addRow("Server", server_);

        auto* buttons = new QHBoxLayout;
        auto* ok = new QPushButton("OK", this);
        auto* cancel = new QPushButton("Cancel", this);
        buttons->addStretch();
        buttons->addWidget(ok);
        buttons->addWidget(cancel);
        layout->addRow(buttons);
        connect(ok, &QPushButton::clicked, this, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    }

    QString shot() const { return shot_->text().trimmed(); }
    SignalSpec signal() const
    {
        SignalSpec sig;
        sig.yExpr = y_->text().trimmed();
        sig.xExpr = x_->text().trimmed();
        sig.experiment = experiment_->text().trimmed();
        sig.serverIp = server_->text().trimmed();
        sig.colorName = colorForIndex(0);
        return sig;
    }

private:
    QLineEdit* shot_ = nullptr;
    QLineEdit* y_ = nullptr;
    QLineEdit* x_ = nullptr;
    QLineEdit* experiment_ = nullptr;
    QLineEdit* server_ = nullptr;
};

class DataSourceDialog final : public QDialog {
public:
    explicit DataSourceDialog(const PlotSpec& base,
                              const QString& currentShot,
                              const QString& sourceIndexDir,
                              DataReadMode inheritedReadMode,
                              QWidget* parent = nullptr)
        : QDialog(parent)
        , defaultShot_(currentShot.trimmed().isEmpty() ? base.shot.trimmed() : currentShot.trimmed())
        , sourceIndexDir_(sourceIndexDir)
        , inheritedReadMode_(inheritedReadMode)
    {
        setWindowTitle("Data Source Setup");
        treeNames_ = readSourceIndexLines(QDir(sourceIndexDir_).filePath("trees.txt"));
        auto* mainLayout = new QVBoxLayout(this);

        rowsHost_ = new QWidget(this);
        rowsLayout_ = new QGridLayout(rowsHost_);
        rowsLayout_->setContentsMargins(0, 0, 0, 0);
        rowsLayout_->setHorizontalSpacing(6);
        rowsLayout_->setVerticalSpacing(4);
        rowsLayout_->addWidget(new QLabel("Shot", rowsHost_), 0, 0);
        rowsLayout_->addWidget(new QLabel("Tree", rowsHost_), 0, 1);
        rowsLayout_->addWidget(new QLabel("Signal", rowsHost_), 0, 2);
        rowsLayout_->addWidget(new QLabel("Server IP", rowsHost_), 0, 3);
        rowsLayout_->addWidget(new QLabel("Color", rowsHost_), 0, 4);
        rowsLayout_->addWidget(new QLabel("Hide", rowsHost_), 0, 5);
        rowsLayout_->addWidget(new QLabel("Data", rowsHost_), 0, 6);
        rowsLayout_->addWidget(new QLabel("", rowsHost_), 0, 7);
        mainLayout->addWidget(rowsHost_);

        QString defaultTree;
        QString defaultServer;
        if (!base.signalSpecs.isEmpty()) {
            defaultTree = base.signalSpecs.front().experiment;
            defaultServer = base.signalSpecs.front().serverIp;
            for (int i = 0; i < base.signalSpecs.size(); ++i) {
                addRow(base.signalSpecs[i], i);
            }
        } else {
            SignalSpec sig;
            sig.experiment = defaultTree;
            sig.serverIp = defaultServer;
            sig.colorName = colorForIndex(0);
            sig.readMode = inheritedReadMode_;
            sig.readModeExplicit = true;
            addRow(sig, 0);
        }

        auto* commandLayout = new QHBoxLayout();
        auto* addCurve = new QPushButton("Add Curve", this);
        auto* ok = new QPushButton("OK", this);
        auto* cancel = new QPushButton("Cancel", this);
        commandLayout->addWidget(addCurve);
        commandLayout->addStretch(1);
        commandLayout->addWidget(ok);
        commandLayout->addWidget(cancel);
        mainLayout->addLayout(commandLayout);

        connect(addCurve, &QPushButton::clicked, this, [this] {
            SignalSpec sig;
            sig.readMode = inheritedReadMode_;
            sig.readModeExplicit = true;
            int activeCount = 0;
            bool copiedDefaults = false;
            if (!rows_.isEmpty()) {
                for (const Row* row : std::as_const(rows_)) {
                    if (!row->deleted) {
                        ++activeCount;
                        if (!copiedDefaults) {
                            sig.serverIp = row->server->text().trimmed();
                            copiedDefaults = true;
                        }
                    }
                }
            }
            sig.colorName = colorForIndex(activeCount);
            addRow(sig, activeCount);
        });
        connect(ok, &QPushButton::clicked, this, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    }

    QVector<SignalSpec> signalSpecs() const
    {
        QVector<SignalSpec> out;
        out.reserve(rows_.size());
        for (const Row* row : rows_) {
            if (row->deleted) {
                continue;
            }
            SignalSpec sig;
            sig.yExpr = row->signal->text().trimmed();
            if (sig.yExpr.isEmpty()) {
                continue;
            }
            const QString shot = row->shot->text().trimmed();
            sig.shot = shotCoveredByDefault(shot) ? QString() : shot;
            sig.xExpr = row->xExpr;
            sig.experiment = row->tree->text().trimmed();
            sig.serverIp = row->server->text().trimmed();
            sig.colorName = row->color.name();
            sig.manualColor = row->manualColor;
            sig.hidden = row->hidden->isChecked();
            sig.readMode =
                row->readModeTouched
                    ? static_cast<DataReadMode>(row->dataMode->currentData().toInt())
                    : row->originalReadMode;
            sig.readModeExplicit = true;
            out.push_back(std::move(sig));
        }
        return out;
    }

private:
    struct Row {
        QLineEdit* shot = nullptr;
        QLineEdit* tree = nullptr;
        QLineEdit* signal = nullptr;
        QLineEdit* server = nullptr;
        QStringListModel* treeModel = nullptr;
        QCompleter* treeCompleter = nullptr;
        QStringListModel* signalModel = nullptr;
        QCompleter* signalCompleter = nullptr;
        QListWidget* reverseTreePopup = nullptr;
        QPushButton* colorButton = nullptr;
        QCheckBox* hidden = nullptr;
        QComboBox* dataMode = nullptr;
        QPushButton* deleteButton = nullptr;
        QColor color;
        QString xExpr;
        QStringList availableSignalNames;
        bool deleted = false;
        bool manualColor = false;
        DataReadMode originalReadMode = DataReadMode::Thin;
        bool readModeTouched = false;
    };

    void addRow(const SignalSpec& sig, int colorIndex)
    {
        auto* row = new Row;
        row->shot = new QLineEdit(sig.shot.trimmed().isEmpty() ? defaultShot_ : sig.shot.trimmed(), rowsHost_);
        row->tree = new QLineEdit(sig.experiment, rowsHost_);
        row->signal = new QLineEdit(sig.yExpr, rowsHost_);
        row->server = new QLineEdit(sig.serverIp, rowsHost_);
        row->treeModel = new QStringListModel(row->tree);
        row->treeCompleter = makeCompleter(row->treeModel, row->tree);
        row->signalModel = new QStringListModel(row->signal);
        row->signalCompleter = makeCompleter(row->signalModel, row->signal);
        row->reverseTreePopup = new QListWidget(this);
        row->reverseTreePopup->setObjectName(QStringLiteral("reverseTreePopup"));
        row->reverseTreePopup->setWindowFlags(Qt::ToolTip
                                              | Qt::FramelessWindowHint
                                              | Qt::WindowDoesNotAcceptFocus);
        row->reverseTreePopup->setAttribute(Qt::WA_ShowWithoutActivating, true);
        row->reverseTreePopup->setFocusPolicy(Qt::NoFocus);
        row->reverseTreePopup->viewport()->setFocusPolicy(Qt::NoFocus);
        row->reverseTreePopup->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        row->reverseTreePopup->setSelectionMode(QAbstractItemView::SingleSelection);
        row->reverseTreePopup->setSelectionBehavior(QAbstractItemView::SelectRows);
        row->reverseTreePopup->setMouseTracking(true);
        row->reverseTreePopup->viewport()->setMouseTracking(true);
        row->reverseTreePopup->setAttribute(Qt::WA_Hover, true);
        row->reverseTreePopup->viewport()->setAttribute(Qt::WA_Hover, true);
        row->reverseTreePopup->setStyleSheet(
            "QListWidget { outline: 0; }"
            "QListWidget::item { padding: 2px 6px; }"
            "QListWidget::item:hover, QListWidget::item:selected {"
            " background: palette(highlight); color: palette(highlighted-text);"
            "}");
        row->reverseTreePopup->hide();
        row->colorButton = new QPushButton(rowsHost_);
        row->hidden = new QCheckBox(rowsHost_);
        row->dataMode = new QComboBox(rowsHost_);
        row->deleteButton = new QPushButton("Delete", rowsHost_);
        row->manualColor = sig.manualColor;
        row->color = QColor(row->manualColor && !sig.colorName.isEmpty() ? sig.colorName : colorForIndex(colorIndex));
        row->xExpr = sig.xExpr;
        row->hidden->setChecked(sig.hidden);
        row->dataMode->addItem("Thin", static_cast<int>(DataReadMode::Thin));
        row->dataMode->addItem("Medium", static_cast<int>(DataReadMode::Medium));
        row->dataMode->addItem("Full", static_cast<int>(DataReadMode::Full));
        // Existing sources display their exact current Rate. Newly added rows
        // were initialized from the current global Rate before reaching here.
        const DataReadMode displayedMode = sig.readMode;
        row->originalReadMode = displayedMode;
        row->dataMode->setCurrentIndex(
            row->dataMode->findData(static_cast<int>(displayedMode)));
        row->shot->setMinimumWidth(72);
        row->signal->setMinimumWidth(150);
        row->server->setMinimumWidth(120);
        row->dataMode->setMinimumWidth(90);
        row->tree->setCompleter(row->treeCompleter);
        row->signal->setCompleter(row->signalCompleter);
        updateTreeCompleter(row, false);
        updateSignalCompleter(row);
        updateColorButton(row);

        const int gridRow = rows_.size() + 1;
        rowsLayout_->addWidget(row->shot, gridRow, 0);
        rowsLayout_->addWidget(row->tree, gridRow, 1);
        rowsLayout_->addWidget(row->signal, gridRow, 2);
        rowsLayout_->addWidget(row->server, gridRow, 3);
        rowsLayout_->addWidget(row->colorButton, gridRow, 4);
        rowsLayout_->addWidget(row->hidden, gridRow, 5, Qt::AlignCenter);
        rowsLayout_->addWidget(row->dataMode, gridRow, 6);
        rowsLayout_->addWidget(row->deleteButton, gridRow, 7);
        rows_.push_back(row);

        connect(row->colorButton, &QPushButton::clicked, this, [this, row] {
            const QColor chosen = QColorDialog::getColor(row->color, this, "Curve Color");
            if (chosen.isValid()) {
                row->color = chosen;
                row->manualColor = true;
                updateColorButton(row);
            }
        });
        connect(row->dataMode,
                qOverload<int>(&QComboBox::activated),
                this,
                [row](int) {
            row->readModeTouched = true;
        });
        connect(row->tree, &QLineEdit::textChanged, this, [this, row] {
            updateSignalCompleter(row);
            if (row->tree->text().trimmed().isEmpty()) {
                updateTreeCompleter(row, true);
            } else if (row->reverseTreePopup) {
                row->reverseTreePopup->hide();
            }
        });
        connect(row->signal, &QLineEdit::textChanged, this, [this, row] {
            refreshSignalSuggestions(row);
            updateTreeCompleter(row, true);
        });
        connect(row->reverseTreePopup, &QListWidget::itemClicked, this, [row](QListWidgetItem* item) {
            if (!row || !row->tree || !row->reverseTreePopup || !item) {
                return;
            }
            row->tree->setText(item->text());
            row->reverseTreePopup->hide();
            if (row->signal) {
                row->signal->setFocus(Qt::OtherFocusReason);
            }
        });
        connect(row->reverseTreePopup, &QListWidget::itemEntered, this, [row](QListWidgetItem* item) {
            if (row && row->reverseTreePopup && item) {
                row->reverseTreePopup->setCurrentItem(item);
            }
        });
        connect(row->deleteButton, &QPushButton::clicked, this, [row] {
            row->deleted = true;
            for (QWidget* widget : {static_cast<QWidget*>(row->shot),
                                    static_cast<QWidget*>(row->tree),
                                    static_cast<QWidget*>(row->signal),
                                    static_cast<QWidget*>(row->server),
                                    static_cast<QWidget*>(row->colorButton),
                                    static_cast<QWidget*>(row->hidden),
                                    static_cast<QWidget*>(row->dataMode),
                                    static_cast<QWidget*>(row->deleteButton)}) {
                widget->hide();
            }
            if (row->reverseTreePopup) {
                row->reverseTreePopup->hide();
            }
        });
    }

    bool shotCoveredByDefault(const QString& shot) const
    {
        const QString trimmedShot = shot.trimmed();
        const QString trimmedDefault = defaultShot_.trimmed();
        if (trimmedShot.isEmpty() || trimmedShot == trimmedDefault) {
            return true;
        }

        const QStringList rowShots = expandedShotList(trimmedShot);
        const QStringList defaultShots = expandedShotList(trimmedDefault);
        if (rowShots.isEmpty() || defaultShots.isEmpty()) {
            return false;
        }

        const QSet<QString> defaultSet(defaultShots.cbegin(), defaultShots.cend());
        for (const QString& rowShot : rowShots) {
            if (!defaultSet.contains(rowShot)) {
                return false;
            }
        }
        return true;
    }

    static QCompleter* makeCompleter(const QStringList& values, QObject* parent)
    {
        return makeCompleter(new QStringListModel(values, parent), parent);
    }

    static QCompleter* makeCompleter(QStringListModel* model, QObject* parent)
    {
        auto* completer = new QCompleter(model, parent);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);
        completer->setCompletionMode(QCompleter::PopupCompletion);
        completer->setMaxVisibleItems(16);
        QAbstractItemView* popup = completer->popup();
        popup->setWindowFlags(Qt::ToolTip
                              | Qt::FramelessWindowHint
                              | Qt::WindowDoesNotAcceptFocus);
        popup->setAttribute(Qt::WA_ShowWithoutActivating, true);
        popup->setFocusPolicy(Qt::NoFocus);
        popup->viewport()->setFocusPolicy(Qt::NoFocus);
        popup->setMouseTracking(true);
        popup->viewport()->setMouseTracking(true);
        popup->setAttribute(Qt::WA_Hover, true);
        popup->viewport()->setAttribute(Qt::WA_Hover, true);
        popup->setSelectionBehavior(QAbstractItemView::SelectRows);
        popup->setStyleSheet(
            "QAbstractItemView { outline: 0; }"
            "QAbstractItemView::item { padding: 2px 6px; }"
            "QAbstractItemView::item:hover, QAbstractItemView::item:selected {"
            " background: palette(highlight); color: palette(highlighted-text);"
            "}");
        return completer;
    }

    static QString sourceIndexFileName(QString tree)
    {
        tree = tree.trimmed().toLower();
        tree.replace(QRegularExpression("[^a-z0-9_-]+"), "_");
        return tree.isEmpty() ? QString() : tree + QStringLiteral(".txt");
    }

    static QStringList readSourceIndexLines(const QString& path)
    {
        QStringList values;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return values;
        }
        QSet<QString> seen;
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            const QString key = line.toLower();
            if (!line.isEmpty() && !seen.contains(key)) {
                values.push_back(line);
                seen.insert(key);
            }
        }
        values.sort(Qt::CaseInsensitive);
        return values;
    }

    static QStringList readSourceIndexSignalLines(const QString& path)
    {
        QStringList values;
        QSet<QString> seen;
        for (const QString& line : readSourceIndexLines(path)) {
            const QStringList nodeNames = sourceIndexSignalNames(line);
            for (const QString& signal : nodeNames) {
                const QString key = signal.toLower();
                if (!signal.isEmpty() && !seen.contains(key)) {
                    values.push_back(signal);
                    seen.insert(key);
                }
            }
        }
        values.sort(Qt::CaseInsensitive);
        return values;
    }

    QStringList signalNamesForTree(const QString& tree)
    {
        const QString resolvedTree = exactTreeName(tree);
        const QString key = resolvedTree.trimmed().toLower();
        if (key.isEmpty()) {
            return allSignalNames();
        }
        if (!signalCache_.contains(key)) {
            const QString path = QDir(QDir(sourceIndexDir_).filePath("signals")).filePath(sourceIndexFileName(resolvedTree));
            signalCache_.insert(key, readSourceIndexSignalLines(path));
        }
        return signalCache_.value(key);
    }

    QString exactTreeName(const QString& tree) const
    {
        const QString key = tree.trimmed();
        if (key.isEmpty()) {
            return {};
        }
        for (const QString& candidate : treeNames_) {
            if (candidate.compare(key, Qt::CaseInsensitive) == 0) {
                return candidate;
            }
        }
        return {};
    }

    void ensureGlobalSignalIndex()
    {
        if (globalSignalIndexLoaded_) {
            return;
        }
        globalSignalIndexLoaded_ = true;

        QSet<QString> seenSignals;
        for (const QString& tree : std::as_const(treeNames_)) {
            const QString treeKey = tree.trimmed().toLower();
            if (treeKey.isEmpty()) {
                continue;
            }

            const QStringList signalNames = signalNamesForTree(tree);
            for (const QString& signalName : signalNames) {
                const QString signalKey = signalIndexKey(signalName);
                if (signalKey.isEmpty()) {
                    continue;
                }
                signalToTrees_[signalKey].insert(tree);
                if (!seenSignals.contains(signalKey)) {
                    globalSignals_.push_back(signalName);
                    seenSignals.insert(signalKey);
                }
            }
        }
        globalSignals_.sort(Qt::CaseInsensitive);
    }

    QStringList allSignalNames()
    {
        ensureGlobalSignalIndex();
        return globalSignals_;
    }

    QStringList exactTreeMatchesForSignal(const QString& signal)
    {
        const QString key = signalIndexKey(signal);
        if (key.isEmpty()) {
            return {};
        }
        ensureGlobalSignalIndex();
        const auto it = signalToTrees_.constFind(key);
        if (it == signalToTrees_.cend()) {
            return {};
        }
        QStringList trees(it.value().cbegin(), it.value().cend());
        trees.sort(Qt::CaseInsensitive);
        return trees;
    }

    static QString signalIndexKey(const QString& signal)
    {
        QString matchText = signal.trimmed();
        if (matchText.startsWith('/')) {
            matchText[0] = QChar('\\');
        }
        const QStringList sourceSignals = sourceIndexSignalNames(matchText);
        QString key = sourceSignals.isEmpty()
                          ? normalizedMdsSignal(matchText).trimmed().toLower()
                          : sourceSignals.front().trimmed().toLower();
        while (key.startsWith(QStringLiteral("\\\\"))) {
            key.remove(0, 1);
        }
        return key;
    }

    void updateTreeCompleter(Row* row, bool showReverseMatches)
    {
        if (!row || row->deleted || !row->tree || !row->signal || !row->treeModel) {
            return;
        }

        const QString signalText = row->signal->text();
        const QString matchKey = signalIndexKey(signalText);
        if (matchKey.isEmpty()) {
            if (row->treeModel->stringList() != treeNames_) {
                row->treeModel->setStringList(treeNames_);
            }
            if (row->reverseTreePopup) {
                row->reverseTreePopup->hide();
            }
            return;
        }

        const QStringList exactTrees = exactTreeMatchesForSignal(signalText);
        const QStringList treeChoices = exactTrees.isEmpty() ? treeNames_ : exactTrees;
        if (row->treeModel->stringList() != treeChoices) {
            row->treeModel->setStringList(treeChoices);
        }
        if (!showReverseMatches || !row->tree->text().trimmed().isEmpty()
            || exactTrees.isEmpty()) {
            if (row->reverseTreePopup) {
                row->reverseTreePopup->hide();
            }
            return;
        }
        showReverseTreePopup(row, exactTrees);
    }

    void showReverseTreePopup(Row* row, const QStringList& trees)
    {
        if (!row || row->deleted || !row->tree || !row->reverseTreePopup || trees.isEmpty()) {
            return;
        }

        QStringList currentTrees;
        currentTrees.reserve(row->reverseTreePopup->count());
        for (int i = 0; i < row->reverseTreePopup->count(); ++i) {
            currentTrees.push_back(row->reverseTreePopup->item(i)->text());
        }
        if (currentTrees != trees) {
            row->reverseTreePopup->clear();
            row->reverseTreePopup->addItems(trees);
        }
        if (row->reverseTreePopup->count() > 0 && !row->reverseTreePopup->currentItem()) {
            row->reverseTreePopup->setCurrentRow(0);
        }

        const QFontMetrics fm(row->reverseTreePopup->font());
        int popupWidth = row->tree->width();
        for (const QString& tree : trees) {
            popupWidth = std::max(popupWidth, fm.horizontalAdvance(tree) + 32);
        }
        const int visibleRows = std::min(8, static_cast<int>(trees.size()));
        const int rowHeight = std::max(fm.height() + 8, row->reverseTreePopup->sizeHintForRow(0));
        row->reverseTreePopup->resize(popupWidth, visibleRows * rowHeight + 4);
        row->reverseTreePopup->move(row->tree->mapToGlobal(QPoint(0, row->tree->height())));
        row->reverseTreePopup->show();
        row->reverseTreePopup->raise();
    }

    static bool hasSignalExpressionSuffix(QString text)
    {
        text = text.trimmed();
        if (text.startsWith('\\') || text.startsWith('/')) {
            text.remove(0, 1);
        }
        static const QRegularExpression simpleNodePattern(
            QStringLiteral(R"(^[A-Za-z][A-Za-z0-9_$:.]*$)"));
        return !text.isEmpty() && !simpleNodePattern.match(text).hasMatch();
    }

    void refreshSignalSuggestions(Row* row)
    {
        if (!row || row->deleted || !row->signal || !row->signalModel) {
            return;
        }
        const QString signalText = row->signal->text().trimmed();
        if (signalText.isEmpty() || hasSignalExpressionSuffix(signalText)) {
            if (!row->signalModel->stringList().isEmpty()) {
                row->signalModel->setStringList({});
            }
            if (row->signalCompleter && row->signalCompleter->popup()) {
                row->signalCompleter->popup()->hide();
            }
            return;
        }

        QString needle = signalText.toLower();
        if (needle.startsWith('\\') || needle.startsWith('/')) {
            needle.remove(0, 1);
        }
        if (needle.isEmpty()) {
            if (!row->signalModel->stringList().isEmpty()) {
                row->signalModel->setStringList({});
            }
            if (row->signalCompleter && row->signalCompleter->popup()) {
                row->signalCompleter->popup()->hide();
            }
            return;
        }

        constexpr int kMaxSuggestions = 128;
        QStringList suggestions;
        suggestions.reserve(kMaxSuggestions);
        for (const QString& candidate : std::as_const(row->availableSignalNames)) {
            QString candidateKey = candidate.toLower();
            if (candidateKey.startsWith('\\')) {
                candidateKey.remove(0, 1);
            }
            if (!candidateKey.contains(needle)) {
                continue;
            }
            suggestions.push_back(candidate);
            if (suggestions.size() >= kMaxSuggestions) {
                break;
            }
        }
        if (row->signalModel->stringList() != suggestions) {
            row->signalModel->setStringList(suggestions);
        }
        QString exactCandidate;
        if (suggestions.size() == 1) {
            exactCandidate = suggestions.front().toLower();
            if (exactCandidate.startsWith('\\')) {
                exactCandidate.remove(0, 1);
            }
        }
        const bool uniqueExactMatch = exactCandidate == needle;
        if (uniqueExactMatch) {
            if (row->signalCompleter && row->signalCompleter->popup()) {
                row->signalCompleter->popup()->hide();
            }
            return;
        }
        if (!row->signalCompleter || !row->signal->hasFocus()) {
            return;
        }
        if (suggestions.isEmpty()) {
            if (row->signalCompleter->popup()) {
                row->signalCompleter->popup()->hide();
            }
            return;
        }
        row->signalCompleter->setCompletionPrefix(needle);
        if (!row->signalCompleter->popup()->isVisible()) {
            row->signalCompleter->complete();
        }
    }

    void updateSignalCompleter(Row* row)
    {
        if (!row || !row->signalModel) {
            return;
        }
        row->availableSignalNames = signalNamesForTree(row->tree->text());
        refreshSignalSuggestions(row);
    }

    static void updateColorButton(Row* row)
    {
        row->colorButton->setFixedSize(36, 20);
        row->colorButton->setText("");
        row->colorButton->setStyleSheet(QString("QPushButton { background: %1; border: 1px solid palette(mid); }")
                                            .arg(row->color.name()));
    }

    QString defaultShot_;
    QString sourceIndexDir_;
    DataReadMode inheritedReadMode_ = DataReadMode::Thin;
    QWidget* rowsHost_ = nullptr;
    QGridLayout* rowsLayout_ = nullptr;
    QStringList treeNames_;
    QStringList globalSignals_;
    QHash<QString, QStringList> signalCache_;
    QHash<QString, QSet<QString>> signalToTrees_;
    QVector<Row*> rows_;
    bool globalSignalIndexLoaded_ = false;
};
