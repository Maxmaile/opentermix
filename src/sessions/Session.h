#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>

// A single SSH session, mapped 1:1 to a "Host" block in ~/.ssh/config.
// Kept as a plain struct on purpose (си-подобный стиль): no methods that hide
// behaviour, just data plus a couple of trivial helpers.
struct Session {
    QString alias;         // Host        (required)
    QString hostName;      // HostName    (required)
    QString user;          // User
    int     port = 0;      // Port        (0 means default 22)
    QString identityFile;  // IdentityFile
    QString proxyJump;     // ProxyJump   (jump host)
    bool    forwardX11 = false;
    bool    compression = false;
    QString group;         // OpenTermix extension: logical folder (stored as comment)
    QStringList extraLines; // unrecognised "Key Value" lines, preserved verbatim

    bool isValid() const { return !alias.isEmpty() && !hostName.isEmpty(); }

    QString displayName() const { return alias.isEmpty() ? hostName : alias; }

    QString target() const {
        const QString h = hostName.isEmpty() ? alias : hostName;
        return user.isEmpty() ? h : (user + "@" + h);
    }
};

Q_DECLARE_METATYPE(Session)
