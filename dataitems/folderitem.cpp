#include "folderitem.h"

FolderItem::FolderItem(QJsonObject jsonObj, const QString &name)
    : QStandardItem(name)
{
    setData(jsonObj["id"].toString(), IdRole);
    setData(jsonObj["first_comic_hash"].toString(), FirstComicHashRole);
    setData(jsonObj["path"].toString(), PathRole);
}

QHash<int, QByteArray> FolderItem::roleNames()
{
    return {
        {Qt::DisplayRole, "display"},
        {FolderItem::IdRole, "folderId"},
        {FolderItem::PathRole, "path"},
    };
}
