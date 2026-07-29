// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "login_bootstrap.hpp"

#include "core/api_auth.hpp"
#include "ui/visuals.hpp"
#include "ssh/ssh_tunnel_manager.hpp"
#include "ui/login_dialog.hpp"

bool ensureApiLoginBeforeMain(const QString& rootPath)
{
    const QHash<QString, QString> properties =
        readApiSettings(rootPath);
    CachedAuth auth;
    if (loadCachedAuth(&auth)
        && !auth.token.trimmed().isEmpty()
        && !tokenExpiresSoon(auth.token)) {
        return true;
    }

    const QString originalApi =
        properties.value(QStringLiteral("ApiUrl")).trimmed();
    QString api = originalApi;
    QString tunnelError;
    SshTunnelManager loginTunnel;
    if (!originalApi.isEmpty()) {
        loginTunnel.prepareUrl(originalApi, &api, &tunnelError);
    }
    if (!api.isEmpty()
        && (!auth.userName.isEmpty() || !auth.password.isEmpty())) {
        const ApiLoginResult result =
            requestApiToken(
                api,
                properties.value(
                    QStringLiteral("Charset"),
                    QStringLiteral("UTF-8")),
                auth.userName,
                auth.password);
        if (result.ok) {
            auth.token = result.token;
            return saveCachedAuth(auth);
        }
    }

    LoginDialog dialog(rootPath, nullptr, api, true);
    dialog.setWindowIcon(appIcon());
    const int result = dialog.exec();
    return result == QDialog::Accepted
           || result == LoginDialog::Skipped;
}
