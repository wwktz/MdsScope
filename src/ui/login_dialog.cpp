#include "mdsscope_internal.h"

LoginDialog::LoginDialog(QString rootPath, QWidget* parent)
    : QDialog(parent), rootPath_(std::move(rootPath))
{
    setWindowTitle("MdsScope Login");
    propertiesPath_ = apiPropertiesPath(rootPath_);

    auto* layout = new QVBoxLayout(this);
    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    auto* form = new QFormLayout;
    userEdit_ = new QLineEdit(this);
    passwordEdit_ = new QLineEdit(this);
    passwordEdit_->setEchoMode(QLineEdit::Password);
    form->addRow("Username", userEdit_);
    form->addRow("Password", passwordEdit_);
    layout->addLayout(form);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    loginButton_ = new QPushButton("Login", this);
    auto* cancel = new QPushButton("Cancel", this);
    buttons->addWidget(loginButton_);
    buttons->addWidget(cancel);
    layout->addLayout(buttons);

    connect(loginButton_, &QPushButton::clicked, this, &LoginDialog::tryLogin);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    loadProperties();
    loadSavedCredentials();
}

void LoginDialog::loadProperties()
{
    properties_ = readApiProperties(rootPath_);
    if (properties_.isEmpty()) {
        statusLabel_->setText("Missing API configuration.");
        loginButton_->setEnabled(false);
        return;
    }
    const QString api = properties_.value("ApiUrl");
    const bool hasToken = !properties_.value("Token").isEmpty();
    const QString configSource = QFile::exists(propertiesPath_) ? propertiesPath_ : "built-in defaults";
    statusLabel_->setText(QString("Config: %1\nAPI: %2\nToken: %3")
                              .arg(configSource, api, hasToken ? "present" : "not present"));
    if (hasToken) {
        QTimer::singleShot(0, this, &QDialog::accept);
    }
}

void LoginDialog::loadSavedCredentials()
{
    QSettings settings(credentialsPath(rootPath_), QSettings::IniFormat);
    userEdit_->setText(settings.value("login/username").toString());
    passwordEdit_->setText(settings.value("login/password").toString());
}

void LoginDialog::tryLogin()
{
    const QString api = properties_.value("ApiUrl");
    const QString charset = properties_.value("Charset", "UTF-8");
    const bool hasToken = !properties_.value("Token").isEmpty();

    if (userEdit_->text().trimmed().isEmpty() && hasToken) {
        accept();
        return;
    }
    if (api.isEmpty()) {
        QMessageBox::warning(this, "Login", "ApiUrl is missing.");
        return;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(api + "/login"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=" + charset);
    request.setRawHeader("User-Agent", "MdsScope/0.1");
    request.setTransferTimeout(5000);

    QJsonObject payload;
    payload.insert("userName", userEdit_->text().trimmed());
    payload.insert("password", passwordEdit_->text());

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QNetworkReply* reply = manager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(5000);
    loop.exec();

    if (!timer.isActive()) {
        reply->abort();
        reply->deleteLater();
        if (hasToken) {
            accept();
            return;
        }
        QMessageBox::warning(this, "Login", "Login request timed out.");
        return;
    }

    const QByteArray body = reply->readAll();
    const auto error = reply->error();
    reply->deleteLater();
    if (error != QNetworkReply::NoError) {
        if (hasToken) {
            accept();
            return;
        }
        QMessageBox::warning(this, "Login", "Login request failed.");
        return;
    }

    const QJsonObject json = QJsonDocument::fromJson(body).object();
    const bool ok = json.value("code").toString() == "20000" || json.value("code").toInt() == 20000;
    const QString token = json.value("data").toObject().value("token").toString();
    if (ok && !token.isEmpty()) {
        properties_.insert("Token", token);
        saveToken(token);
        saveCredentials();
        accept();
        return;
    }
    QMessageBox::warning(this, "Login", "Failed to login.");
}

void LoginDialog::saveCredentials() const
{
    QSettings settings(credentialsPath(rootPath_), QSettings::IniFormat);
    settings.setValue("login/username", userEdit_->text().trimmed());
    settings.setValue("login/password", passwordEdit_->text());
}

bool LoginDialog::saveToken(const QString& token)
{
    QFile in(propertiesPath_);
    QStringList lines;
    bool found = false;
    if (in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!in.atEnd()) {
            QString line = QString::fromUtf8(in.readLine());
            if (line.trimmed().startsWith("Token=") || line.trimmed().startsWith("Token:")) {
                line = "Token=" + token + "\n";
                found = true;
            }
            lines.push_back(line);
        }
    }
    if (!found) {
        if (lines.isEmpty()) {
            lines.push_back("ApiUrl=" + properties_.value("ApiUrl") + "\n");
            lines.push_back("Authorization_Prefix=" + properties_.value("Authorization_Prefix", "Bearer") + "\n");
            lines.push_back("Charset=" + properties_.value("Charset", "UTF-8") + "\n");
        }
        lines.push_back("Token=" + token + "\n");
    }
    QFile out(propertiesPath_);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }
    for (const QString& line : std::as_const(lines)) {
        out.write(line.toUtf8());
    }
    return true;
}
