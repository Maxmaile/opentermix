#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

// OpenTermix's own view of the session tree: the ordered list of folders and which
// folder each host (by its ~/.ssh/config alias) belongs to. It is stored as JSON
// in the app data dir, completely separate from ~/.ssh/config, so organising
// sessions into folders by drag-and-drop never rewrites the ssh config.
class SessionLayout {
public:
    // Load the JSON file if it exists. Returns true when a file was present (a
    // false return is the cue to seed the layout from any legacy config groups).
    bool load();
    void save() const;

    const QStringList &folders() const { return folders_; }
    QString groupOf(const QString &alias) const { return groups_.value(alias); }

    // Grouping. An empty folder name moves the host back to the top level.
    void setGroup(const QString &alias, const QString &folder);
    void renameHost(const QString &oldAlias, const QString &newAlias);
    void removeHost(const QString &alias);

    // Folders.
    bool hasFolder(const QString &name) const { return folders_.contains(name); }
    bool addFolder(const QString &name);  // false if it already exists
    void renameFolder(const QString &oldName, const QString &newName);
    void removeFolder(const QString &name); // also ungroups the hosts it held

    // One-time migration: adopt the folders/grouping that older versions kept in
    // the ssh config, so upgrading users do not lose their tree.
    void seedFrom(const QStringList &folders, const QHash<QString, QString> &groups);

private:
    static QString filePath();

    QStringList folders_;             // ordered, includes empty folders
    QHash<QString, QString> groups_;  // alias -> folder (absent => top level)
};
