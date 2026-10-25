#include "comicitem.h"

ComicItem::ComicItem(QJsonObject jsonObj, const QString &name)
    : QStandardItem(name)
{
    setData(jsonObj["id"].toString(), IdRole);
    setData(jsonObj["hash"].toString(), HashRole);
    setData(jsonObj["num_pages"].toInt(), PageCountRole);
    setData(jsonObj["current_page"].toInt(), CurrentPageRole);
    setData(jsonObj["type"].toInt(), TypeRole);
    setData(jsonObj["added"].toInteger(), AddedTimeRole);
}

QHash<int, QByteArray> ComicItem::roleNames()
{
    return {
        {Qt::DisplayRole, "display"},
        {ComicItem::IdRole, "comicId"},
        {ComicItem::HashRole, "hash"},
        {ComicItem::PageCountRole, "pageCount"},
        {ComicItem::CurrentPageRole, "currentPage"},
        {ComicItem::TypeRole, "type"},
        {ComicItem::AddedTimeRole, "addedTime"},
    };
}
