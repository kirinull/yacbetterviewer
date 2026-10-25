#include "libraryitem.h"

LibraryItem::LibraryItem(int id, const QString &name, const QString &uuid)
    : QStandardItem(QIcon::fromTheme(QIcon::ThemeIcon::FolderOpen), name)
{
    setData(id, IdRole);
    setData(uuid, UuidRole);
}

QHash<int, QByteArray> LibraryItem::roleNames()
{
    return {
        {Qt::DisplayRole, "display"},
        {LibraryItem::IdRole, "libraryId"},
        {LibraryItem::UuidRole, "uuid"},
    };
}
