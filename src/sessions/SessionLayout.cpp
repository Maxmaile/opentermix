#include "sessions/SessionLayout.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

QString SessionLayout::filePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir + QStringLiteral("/sessions.json");
}

bool SessionLayout::load()
{
    folders_.clear();
    groups_.clear();

    QFile f(filePath());
    if (!f.exists())
        return false;
    if (!f.open(QIODevice::ReadOnly))
        return false;

    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();

    for (const QJsonValue &v : root.value(QStringLiteral("folders")).toArray()) {
        const QString name = v.toString();
        if (!name.isEmpty() && !folders_.contains(name))
            folders_.append(name);
    }

    const QJsonObject groups = root.value(QStringLiteral("groups")).toObject();
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        const QString folder = it.value().toString();
        if (folder.isEmpty())
            continue;
        groups_.insert(it.key(), folder);
        if (!folders_.contains(folder))
            folders_.append(folder);
    }
    return true;
}

void SessionLayout::save() const
{
    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonArray folders;
    for (const QString &name : folders_)
        folders.append(name);

    QJsonObject groups;
    for (auto it = groups_.constBegin(); it != groups_.constEnd(); ++it) {
        if (!it.value().isEmpty())
            groups.insert(it.key(), it.value());
    }

    QJsonObject root;
    root.insert(QStringLiteral("folders"), folders);
    root.insert(QStringLiteral("groups"), groups);

    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void SessionLayout::setGroup(const QString &alias, const QString &folder)
{
    if (folder.isEmpty()) {
        groups_.remove(alias);
        return;
    }
    groups_.insert(alias, folder);
    if (!folders_.contains(folder))
        folders_.append(folder);
}

void SessionLayout::renameHost(const QString &oldAlias, const QString &newAlias)
{
    if (oldAlias == newAlias)
        return;
    auto it = groups_.find(oldAlias);
    if (it == groups_.end())
        return;
    const QString folder = it.value();
    groups_.erase(it);
    groups_.insert(newAlias, folder);
}

void SessionLayout::removeHost(const QString &alias)
{
    groups_.remove(alias);
}

bool SessionLayout::addFolder(const QString &name)
{
    if (name.isEmpty() || folders_.contains(name))
        return false;
    folders_.append(name);
    return true;
}

void SessionLayout::renameFolder(const QString &oldName, const QString &newName)
{
    if (oldName.isEmpty() || newName.isEmpty() || oldName == newName)
        return;
    const int idx = folders_.indexOf(oldName);
    if (idx >= 0)
        folders_[idx] = newName;
    else if (!folders_.contains(newName))
        folders_.append(newName);
    for (auto it = groups_.begin(); it != groups_.end(); ++it) {
        if (it.value() == oldName)
            it.value() = newName;
    }
}

void SessionLayout::removeFolder(const QString &name)
{
    folders_.removeAll(name);
    for (auto it = groups_.begin(); it != groups_.end();) {
        if (it.value() == name)
            it = groups_.erase(it);
        else
            ++it;
    }
}

void SessionLayout::seedFrom(const QStringList &folders, const QHash<QString, QString> &groups)
{
    folders_ = folders;
    groups_.clear();
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        if (it.value().isEmpty())
            continue;
        groups_.insert(it.key(), it.value());
        if (!folders_.contains(it.value()))
            folders_.append(it.value());
    }
}
