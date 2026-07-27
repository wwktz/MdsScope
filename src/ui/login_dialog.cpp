// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
#include "login_dialog.hpp"

namespace {
class ThemeCheckBox final : public QCheckBox {
public:
    using QCheckBox::QCheckBox;

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QCheckBox::paintEvent(event);

        QStyleOptionButton option;
        initStyleOption(&option);
        const QRect indicator = style()->subElementRect(
            QStyle::SE_CheckBoxIndicator, &option, this);
        if (!indicator.isValid()) {
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor boxColor = palette().color(QPalette::Text);
        const QRectF boxRect = QRectF(indicator).adjusted(0.5, 0.5, -0.5, -0.5);
        if (!isChecked()) {
            painter.setPen(QPen(boxColor, 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(boxRect, 2.0, 2.0);
            return;
        }

        const QColor checkColor = palette().color(QPalette::Base);
        painter.setPen(Qt::NoPen);
        painter.setBrush(boxColor);
        painter.drawRoundedRect(boxRect, 2.0, 2.0);

        const qreal x = indicator.x();
        const qreal y = indicator.y();
        const qreal width = indicator.width();
        const qreal height = indicator.height();
        QPen checkPen(checkColor, std::max<qreal>(1.5, width * 0.13));
        checkPen.setCapStyle(Qt::RoundCap);
        checkPen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(checkPen);
        QPainterPath check;
        check.moveTo(x + width * 0.22, y + height * 0.52);
        check.lineTo(x + width * 0.43, y + height * 0.72);
        check.lineTo(x + width * 0.79, y + height * 0.28);
        painter.drawPath(check);
    }
};
}

LoginDialog::LoginDialog(QString rootPath, QWidget* parent, QString apiOverride, bool allowSkip)
    : QDialog(parent), rootPath_(std::move(rootPath)), apiOverride_(std::move(apiOverride))
{
    setWindowTitle("Login");
    setModal(true);
    setFixedWidth(420);
    QString styleSheet =
        "QDialog { background: palette(base); }"
        "QLabel#title { font-size: 24px; font-weight: 600; color: palette(text); }"
        "QLabel#subtitle { color: palette(mid); }"
        "QLabel#status { color: #b91c1c; }"
        "QCheckBox { color: palette(text); }"
        "QLineEdit { min-height: 32px; padding: 4px 8px; border: 1px solid palette(mid); border-radius: 4px; }"
        "QLineEdit:focus { border: 1px solid palette(highlight); }"
        "QPushButton { min-height: 32px; padding: 4px 14px; border-radius: 4px; }"
        "QPushButton#primary { background: palette(highlight); color: palette(highlighted-text); border: 1px solid palette(highlight); }";
    if (QApplication::palette().color(QPalette::Window).lightness() >= 128) {
        styleSheet +=
            "QLabel { background: transparent; }"
            "QLabel#subtitle { color: #64748b; }"
            "QLineEdit { background: #ffffff; color: #111827; border-color: #cbd5e1; selection-background-color: #2563eb; selection-color: #ffffff; }"
            "QLineEdit:focus { border-color: #2563eb; }";
    }
    setStyleSheet(styleSheet);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(8);

    auto* title = new QLabel("MdsScope", this);
    title->setObjectName("title");
    layout->addWidget(title);

    auto* subtitle = new QLabel("Sign in to access EAST shot metadata.", this);
    subtitle->setObjectName("subtitle");
    layout->addWidget(subtitle);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName("status");
    statusLabel_->setWordWrap(true);
    statusLabel_->hide();
    layout->addWidget(statusLabel_);

    layout->addSpacing(8);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(10);
    userEdit_ = new QLineEdit(this);
    userEdit_->setPlaceholderText("Username");
    passwordEdit_ = new QLineEdit(this);
    passwordEdit_->setPlaceholderText("Password");
    passwordEdit_->setEchoMode(QLineEdit::Password);
    rememberPassword_ = new ThemeCheckBox("Remember password", this);
    form->addRow("Username", userEdit_);
    form->addRow("Password", passwordEdit_);
    form->addRow(QString(), rememberPassword_);
    layout->addLayout(form);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* cancel = new QPushButton("Cancel", this);
    loginButton_ = new QPushButton("Login", this);
    loginButton_->setObjectName("primary");
    if (!allowSkip) {
        logoutButton_ = new QPushButton("Logout", this);
        buttons->addWidget(logoutButton_);
        connect(logoutButton_, &QPushButton::clicked, this, &LoginDialog::logout);
    }
    buttons->addWidget(cancel);
    if (allowSkip) {
        auto* skip = new QPushButton("Skip for now", this);
        skip->setToolTip("Continue to MdsScope and configure SSH or login later from the toolbar");
        buttons->addWidget(skip);
        connect(skip, &QPushButton::clicked, this, [this] { done(Skipped); });
    }
    buttons->addWidget(loginButton_);
    layout->addLayout(buttons);

    connect(loginButton_, &QPushButton::clicked, this, &LoginDialog::tryLogin);
    connect(passwordEdit_, &QLineEdit::returnPressed, this, &LoginDialog::tryLogin);
    connect(userEdit_, &QLineEdit::returnPressed, passwordEdit_, qOverload<>(&QWidget::setFocus));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    loadProperties();
}

void LoginDialog::loadProperties()
{
    properties_ = readApiSettings(rootPath_);
    if (!apiOverride_.trimmed().isEmpty()) {
        properties_.insert(QStringLiteral("ApiUrl"), apiOverride_.trimmed());
    }
    CachedAuth auth;
    if (loadCachedAuth(&auth)) {
        userEdit_->setText(auth.userName);
        passwordEdit_->setText(auth.password);
        rememberPassword_->setChecked(!auth.password.isEmpty());
        if (logoutButton_) {
            logoutButton_->setEnabled(!auth.userName.isEmpty()
                                      || !auth.password.isEmpty()
                                      || !auth.token.isEmpty());
        }
    } else if (logoutButton_) {
        logoutButton_->setEnabled(false);
    }
    if (properties_.value("ApiUrl").trimmed().isEmpty()) {
        statusLabel_->setText("Missing API configuration.");
        statusLabel_->show();
        loginButton_->setEnabled(false);
        return;
    }
    statusLabel_->clear();
    statusLabel_->hide();
    if (userEdit_->text().trimmed().isEmpty()) {
        userEdit_->setFocus();
    } else {
        passwordEdit_->setFocus();
    }
}

void LoginDialog::tryLogin()
{
    if (loginInProgress_) {
        return;
    }
    const QString api = properties_.value("ApiUrl");
    const QString charset = properties_.value("Charset", "UTF-8");

    if (api.trimmed().isEmpty()) {
        QMessageBox::warning(this, "Login", "Missing API URL.");
        return;
    }

    const QString userName = userEdit_->text().trimmed();
    const QString password = passwordEdit_->text();
    const bool rememberPassword = rememberPassword_->isChecked();
    loginInProgress_ = true;
    loginButton_->setEnabled(false);
    statusLabel_->setText(QStringLiteral("Signing in..."));
    statusLabel_->show();
    auto* watcher = new QFutureWatcher<ApiLoginResult>(this);
    connect(watcher, &QFutureWatcher<ApiLoginResult>::finished, this,
            [this, watcher, userName, password, rememberPassword] {
        const ApiLoginResult result = watcher->result();
        watcher->deleteLater();
        loginInProgress_ = false;
        loginButton_->setEnabled(true);
        if (result.ok && !result.token.isEmpty()) {
            CachedAuth auth;
            loadCachedAuth(&auth);
            auth.userName = userName;
            auth.password = rememberPassword ? password : QString();
            auth.token = result.token;
            saveCachedAuth(auth);
            accept();
            return;
        }
        statusLabel_->setText(result.error.isEmpty() ? QStringLiteral("Failed to login.") : result.error);
        statusLabel_->show();
        userEdit_->clear();
        passwordEdit_->clear();
        userEdit_->setFocus();
    });
    watcher->setFuture(QtConcurrent::run([api, charset, userName, password] {
        return requestApiToken(api, charset, userName, password);
    }));
}

void LoginDialog::logout()
{
    if (!clearCachedApiAuth()) {
        statusLabel_->setText(QStringLiteral("Failed to clear saved login information."));
        statusLabel_->show();
        return;
    }
    done(LoggedOut);
}
