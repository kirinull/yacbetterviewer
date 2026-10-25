#pragma once

#include <QJsonObject>
#include <QStandardItem>
#include <QQmlEngine>

class ComicItem : public QStandardItem
{
    Q_GADGET
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        HashRole,
        PageCountRole,
        CurrentPageRole,
        TypeRole,
        AddedTimeRole,
    };
    Q_ENUM(Roles)

    explicit ComicItem(QJsonObject jsonObj, const QString &name);
    static QHash<int, QByteArray> roleNames();
};
