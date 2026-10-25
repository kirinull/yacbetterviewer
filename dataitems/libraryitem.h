#pragma once

#include <QStandardItem>

class LibraryItem : public QStandardItem
{
    Q_GADGET
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        UuidRole
    };
    Q_ENUM(Roles)

    explicit LibraryItem(int id, const QString &name, const QString &uuid);
    static QHash<int, QByteArray> roleNames();
};
