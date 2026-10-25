#pragma once

#include <QJsonObject>
#include <QStandardItem>
#include <QQmlEngine>

class FolderItem : public QStandardItem
{
    Q_GADGET
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        FirstComicHashRole,
        PathRole,
    };
    Q_ENUM(Roles)

    explicit FolderItem(QJsonObject jsonObj, const QString &name);
    static QHash<int, QByteArray> roleNames();
};
