// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>
#include <QHash>
#include <QString>

class QLabel;
class QLineEdit;
class QPushButton;

class LoginDialog final : public QDialog {
    Q_OBJECT

public:
    enum ResultCode {
        Skipped = QDialog::Accepted + 1,
    };

    explicit LoginDialog(QString rootPath,
                         QWidget* parent = nullptr,
                         QString apiOverride = {},
                         bool allowSkip = false);

private:
    void loadProperties();
    void tryLogin();

    QString rootPath_;
    QString apiOverride_;
    QHash<QString, QString> properties_;
    QLineEdit* userEdit_ = nullptr;
    QLineEdit* passwordEdit_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* loginButton_ = nullptr;
    bool loginInProgress_ = false;
};
