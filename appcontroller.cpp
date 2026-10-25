#include "appcontroller.h"

#include "dataitems/comicitem.h"
#include "dataitems/folderitem.h"
#include "dataitems/libraryitem.h"

#include <chrono>
#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QRestAccessManager>
#include <QRestReply>
#include <QSet>
#include <QSettings>

using namespace std::chrono_literals;

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_networkAccessManager(new QNetworkAccessManager(this))
    , m_restAccessManager(new QRestAccessManager(m_networkAccessManager, this))
    , m_librariesModel(new QStandardItemModel(this))
    , m_comicsModel(new QStandardItemModel(this))
    , m_foldersModel(new QStandardItemModel(this))
{
    m_librariesModel->setItemRoleNames(LibraryItem::roleNames());
    m_comicsModel->setItemRoleNames(ComicItem::roleNames());
    m_foldersModel->setItemRoleNames(FolderItem::roleNames());

    // 已读标记存在本地设置里，不动磁盘上的文件
    QSettings settings;
    const QStringList archived = settings.value(QStringLiteral("finished")).toStringList();
    for (const QString &id : archived) {
        if (!id.isEmpty()) {
            m_finished.insert(id);
        }
    }
    m_finishedCount = m_finished.size();

    const QStringList favs = settings.value(QStringLiteral("favorites")).toStringList();
    for (const QString &id : favs) {
        if (!id.isEmpty()) {
            m_favorites.insert(id);
        }
    }
    m_favoriteCount = m_favorites.size();

    connect(this, &AppController::currentComicIdChanged, this, [this](){
        // "selected" is "current"
        // 切到下一本时不要关阅读器，否则会闪一下详情页
        if (!m_switchingComic) {
            setProperty("selectedComicOpened", false);
        }
        m_serverProgress = 0;
    });
}

void AppController::connectServer(QUrl serverBaseUrl)
{
    setProperty("connectionState", Connecting);
    serverBaseUrl.setPath("/v2/");
    QNetworkRequestFactory api(serverBaseUrl);
    QNetworkRequest req(api.createRequest("version"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(5s);
    m_restAccessManager->get(req, this, [=](QRestReply &reply){
        if (reply.isSuccess()) {
            m_requestFactory.setBaseUrl(serverBaseUrl);
            m_requestFactory.clearCommonHeaders();
            QHttpHeaders commonHeaders;
            commonHeaders.append("X-Request-Id", "114514");
            m_requestFactory.setCommonHeaders(commonHeaders);
            setProperty("connectionState", Connected);
            setProperty("lastErrorMessage", "");
        } else {
            setProperty("connectionState", NotConnected);
            setProperty("lastErrorMessage", QStringLiteral("%1: %2").arg(reply.httpStatus()).arg(reply.errorString()));
        }
    });
}

void AppController::updateLibraries()
{
    m_restAccessManager->get(apiFactory().createRequest("libraries"), this, [=](QRestReply &reply){
        if (reply.isSuccess()) {
            std::optional<QJsonDocument> libraries = reply.readJson();
            if (libraries && !(*libraries).isEmpty() && (*libraries).isArray()) {
                const QJsonArray array = (*libraries).array();
                m_librariesModel->clear();
                for (const QJsonValue & value : array) {
                    QJsonObject libraryObj = value.toObject();
                    m_librariesModel->appendRow(new LibraryItem(libraryObj["id"].toInt(),
                                                                libraryObj["name"].toString(),
                                                                libraryObj["uuid"].toString()));
                }
                emit librariesLoaded(array.count());
            }
        }
    });
}

void AppController::updateComicsInFolder(QString folderId)
{
    m_restAccessManager->get(apiFactory().createRequest(QString("library/%1/folder/%2/content")
                                                            .arg(m_currentLibraryId).arg(folderId)),
                             this, [=](QRestReply &reply){
        if (reply.isSuccess()) {
            std::optional<QJsonDocument> libraries = reply.readJson();
            if (libraries && !(*libraries).isEmpty() && (*libraries).isArray()) {
                const QJsonArray array = (*libraries).array();
                m_foldersModel->clear();
                // always add a top-level folder entry...for now
                QJsonObject root;
                root["id"] = "1";
                m_foldersModel->appendRow(new FolderItem(root, "Root"));

                m_allComics = QJsonArray();
                for (const QJsonValue & value : array) {
                    QJsonObject obj = value.toObject();
                    if (obj["type"].toString() == QLatin1String("folder")) {
                        m_foldersModel->appendRow(new FolderItem(obj, obj["folder_name"].toString()));
                    } else {
                        m_allComics.append(obj);
                    }
                }
                m_totalComicCount = m_allComics.size();
                emit totalComicCountChanged();
                repopulateComicsModel();
            }
        }
    });
}

void AppController::repopulateComicsModel()
{
    m_comicsModel->clear();

    const QString needle = m_filterText.trimmed();

    // 先过滤（搜索词 + 已读归档状态），再排序
    QList<QJsonObject> rows;
    rows.reserve(m_allComics.size());
    for (const QJsonValue & value : std::as_const(m_allComics)) {
        const QJsonObject obj = value.toObject();
        if (!needle.isEmpty()
            && !obj["file_name"].toString().contains(needle, Qt::CaseInsensitive)) {
            continue;
        }
        const bool archived = m_finished.contains(obj["id"].toString());
        if (m_showFavorites) {
            // 收藏视图：只看收藏，不管是否看过
            if (!m_favorites.contains(obj["id"].toString())) {
                continue;
            }
        } else if (archived != m_showFinished) {
            continue;
        }
        rows.append(obj);
    }

    auto keySize = [](const QJsonObject &o) {
        return o["file_size"].toString().toLongLong();
    };
    auto keyAdded = [](const QJsonObject &o) {
        return static_cast<qlonglong>(o["added"].toDouble());
    };
    auto keyPages = [](const QJsonObject &o) {
        return static_cast<qlonglong>(o["num_pages"].toInt());
    };

    switch (m_sortMode) {
    case SortRandom:
        std::sort(rows.begin(), rows.end(), [this](const QJsonObject &a, const QJsonObject &b) {
            return m_randomRank.value(a["id"].toString(), 0)
                 < m_randomRank.value(b["id"].toString(), 0);
        });
        break;
    case SortBySize:
        std::sort(rows.begin(), rows.end(), [&](const QJsonObject &a, const QJsonObject &b) {
            return m_sortAscending ? keySize(a) < keySize(b) : keySize(a) > keySize(b);
        });
        break;
    case SortByPages:
        std::sort(rows.begin(), rows.end(), [&](const QJsonObject &a, const QJsonObject &b) {
            return m_sortAscending ? keyPages(a) < keyPages(b) : keyPages(a) > keyPages(b);
        });
        break;
    case SortByAuthor:
        std::sort(rows.begin(), rows.end(), [&](const QJsonObject &a, const QJsonObject &b) {
            const QString aa = extractAuthor(a["file_name"].toString());
            const QString ab = extractAuthor(b["file_name"].toString());
            const int cmp = QString::localeAwareCompare(aa, ab);
            if (cmp != 0) return m_sortAscending ? cmp < 0 : cmp > 0;
            // 同作者内按文件名，保证结果稳定
            const int nc = QString::localeAwareCompare(a["file_name"].toString(),
                                                       b["file_name"].toString());
            return m_sortAscending ? nc < 0 : nc > 0;
        });
        break;
    case SortByName:
        std::sort(rows.begin(), rows.end(), [&](const QJsonObject &a, const QJsonObject &b) {
            const int cmp = QString::localeAwareCompare(a["file_name"].toString(),
                                                        b["file_name"].toString());
            return m_sortAscending ? cmp < 0 : cmp > 0;
        });
        break;
    case SortByAdded:
    default:
        std::sort(rows.begin(), rows.end(), [&](const QJsonObject &a, const QJsonObject &b) {
            return m_sortAscending ? keyAdded(a) < keyAdded(b) : keyAdded(a) > keyAdded(b);
        });
        break;
    }

    for (const QJsonObject &obj : std::as_const(rows)) {
        m_comicsModel->appendRow(new ComicItem(obj, obj["file_name"].toString()));
    }

    if (m_filteredCount != rows.size()) {
        m_filteredCount = rows.size();
        emit filteredCountChanged();
    }
}

// 文件名格式普遍为 "[作者 (画师)] 标题 [汉化组]..."，取第一个方括号内的内容作为作者。
// 没有方括号时退而取开头到第一个空格之前的部分。
QString AppController::extractAuthor(const QString &fileName)
{
    const int lb = fileName.indexOf(QLatin1Char('['));
    if (lb >= 0) {
        const int rb = fileName.indexOf(QLatin1Char(']'), lb);
        if (rb > lb) {
            const QString inner = fileName.mid(lb + 1, rb - lb - 1).trimmed();
            // 去掉画师括号部分："作者 (画师)" -> "作者"
            const int paren = inner.indexOf(QLatin1Char('('));
            return (paren > 0 ? inner.left(paren) : inner).trimmed();
        }
    }
    const int sp = fileName.indexOf(QLatin1Char(' '));
    return (sp > 0 ? fileName.left(sp) : fileName).trimmed();
}

void AppController::setSort(int mode, bool ascending)
{
    m_sortMode = mode;
    m_sortAscending = ascending;
    emit sortModeChanged();
    emit sortAscendingChanged();

    // 每次选随机都重新打乱，否则同一个顺序就不再是“随机”
    if (mode == SortRandom) {
        resort();
    } else {
        repopulateComicsModel();
    }
}

void AppController::resort()
{
    m_randomRank.clear();
    for (const QJsonValue & value : std::as_const(m_allComics)) {
        m_randomRank.insert(value.toObject()["id"].toString(), QRandomGenerator::global()->generate());
    }
    repopulateComicsModel();
}

void AppController::markFinished(const QString &comicId, bool finished)
{
    if (comicId.isEmpty()) return;

    if (finished) {
        m_finished.insert(comicId);
    } else {
        m_finished.remove(comicId);
    }

    QSettings settings;
    settings.setValue(QStringLiteral("finished"), QStringList(m_finished.values()));

    m_finishedCount = m_finished.size();
    emit finishedCountChanged();

    // 故意不在这里重建列表。
    // 正在阅读的书被归档后会从 m_comicsModel 里消失，currentComicModelIndex()
    // 返回无效索引，pageCount 变 0，阅读器直接崩到下面的详情页。
    // 列表放到关闭阅读器时再刷新。
}

bool AppController::isFinished(const QString &comicId) const
{
    return m_finished.contains(comicId);
}

void AppController::setShowFinished(bool show)
{
    if (m_showFinished == show) return;
    m_showFinished = show;
    if (show) {
        m_showFavorites = false;
        emit showFavoritesChanged();
    }
    emit showFinishedChanged();
    repopulateComicsModel();
}

void AppController::toggleFavorite(const QString &comicId)
{
    if (comicId.isEmpty()) return;

    if (m_favorites.contains(comicId)) {
        m_favorites.remove(comicId);
    } else {
        m_favorites.insert(comicId);
    }

    QSettings settings;
    settings.setValue(QStringLiteral("favorites"), QStringList(m_favorites.values()));

    m_favoriteCount = m_favorites.size();
    emit favoriteCountChanged();

    // 与归档同理：不在阅读中重建列表，避免当前书被抽走后索引失效
    if (!m_showFavorites) {
        repopulateComicsModel();
    }
}

bool AppController::isFavorite(const QString &comicId) const
{
    return m_favorites.contains(comicId);
}

void AppController::setShowFavorites(bool show)
{
    if (m_showFavorites == show) return;
    m_showFavorites = show;
    if (show) {
        m_showFinished = false;
        emit showFinishedChanged();
    }
    emit showFavoritesChanged();
    repopulateComicsModel();
}

// 在当前排序后的列表里找当前书的位置，优先下一本同作者的，
// 同作者没有剩余就跳到下一个作者的第一本。
bool AppController::hasNextComic() const
{
    if (m_currentComicId.isEmpty()) return false;

    QString curAuthor;
    int curIdx = -1;
    for (int i = 0; i < m_comicsModel->rowCount(); ++i) {
        QModelIndex idx = m_comicsModel->index(i, 0);
        if (m_comicsModel->data(idx, ComicItem::IdRole).toString() == m_currentComicId) {
            curIdx = i;
            curAuthor = extractAuthor(m_comicsModel->data(idx, Qt::DisplayRole).toString());
            break;
        }
    }
    if (curIdx < 0) return false;

    // 先往后找同作者
    for (int i = curIdx + 1; i < m_comicsModel->rowCount(); ++i) {
        QModelIndex idx = m_comicsModel->index(i, 0);
        if (extractAuthor(m_comicsModel->data(idx, Qt::DisplayRole).toString()) == curAuthor) {
            return true;
        }
    }
    // 再找下一个作者（任意后续条目）
    return curIdx + 1 < m_comicsModel->rowCount();
}

void AppController::openNextComic()
{
    if (m_currentComicId.isEmpty()) return;

    QString curAuthor;
    int curIdx = -1;
    for (int i = 0; i < m_comicsModel->rowCount(); ++i) {
        QModelIndex idx = m_comicsModel->index(i, 0);
        if (m_comicsModel->data(idx, ComicItem::IdRole).toString() == m_currentComicId) {
            curIdx = i;
            curAuthor = extractAuthor(m_comicsModel->data(idx, Qt::DisplayRole).toString());
            break;
        }
    }
    if (curIdx < 0) return;

    QString nextId;
    for (int i = curIdx + 1; i < m_comicsModel->rowCount(); ++i) {
        QModelIndex idx = m_comicsModel->index(i, 0);
        if (extractAuthor(m_comicsModel->data(idx, Qt::DisplayRole).toString()) == curAuthor) {
            nextId = m_comicsModel->data(idx, ComicItem::IdRole).toString();
            break;
        }
    }
    if (nextId.isEmpty() && curIdx + 1 < m_comicsModel->rowCount()) {
        nextId = m_comicsModel->data(m_comicsModel->index(curIdx + 1, 0),
                                     ComicItem::IdRole).toString();
    }
    if (nextId.isEmpty()) return;

    m_switchingComic = true;
    setProperty("selectedComicId", nextId);
    m_switchingComic = false;
    openComic();
}

void AppController::setFilter(const QString &text)
{
    if (m_filterText == text) {
        return;
    }
    m_filterText = text;
    emit filterTextChanged();
    repopulateComicsModel();
}

void AppController::openComic()
{
    // 记下服务端保存的进度，供阅读器决定初始页
    m_serverProgress = 0;
    for (const QJsonValue & value : std::as_const(m_allComics)) {
        const QJsonObject obj = value.toObject();
        if (obj["id"].toString() == m_currentComicId) {
            m_serverProgress = obj["current_page"].toInt();
            break;
        }
    }

    // 服务端要求 X-Request-Id 头，缺失时会直接返回 404（不是 424）。
    // requestFactory 的 commonHeaders 在这里没稳定带到，所以显式补上。
    QNetworkRequest openReq = apiFactory().createRequest(QString("library/%1/comic/%2/remote")
                                                         .arg(m_currentLibraryId).arg(m_currentComicId));
    openReq.setRawHeader("X-Request-Id", "114514");

    m_restAccessManager->get(openReq,
                            this, [=](QRestReply &reply){
        if (reply.isSuccess()) {
            setProperty("selectedComicOpened", true);
        } else {
            setProperty("selectedComicOpened", false);
            setProperty("lastErrorMessage", QStringLiteral("打开失败: %1").arg(reply.httpStatus()));
        }
    });
}

void AppController::closeComic()
{
    // the api seems not to proceed by the server, so we simply do nothing but set the property
    setProperty("selectedComicOpened", false);

    // 阅读期间可能产生了归档标记，这里才刷新列表，
    // 避免在阅读中把当前书从模型里抽走导致索引失效。
    repopulateComicsModel();
}

int AppController::savedProgress(const QString &comicId) const
{
    if (comicId.isEmpty()) {
        return 0;
    }
    QSettings settings;
    return settings.value(QStringLiteral("progress/%1").arg(comicId), 0).toInt();
}

void AppController::saveProgress(const QString &comicId, int page)
{
    if (comicId.isEmpty() || page <= 0) {
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("progress/%1").arg(comicId), page);
}

int AppController::serverProgress() const
{
    return m_serverProgress;
}

QString AppController::coverImageSource(QString comicHash)
{
    QUrl url(apiFactory().baseUrl());
    url.setPath(url.path() + "library/" + QString::number(m_currentLibraryId) + "/cover/" + comicHash + ".jpg");
    return url.toString();
}

QModelIndex AppController::currentComicModelIndex()
{
    if (m_currentComicId.isEmpty()) return QModelIndex();
    for (int i = 0; i < m_comicsModel->rowCount(); i++) {
        QModelIndex index = m_comicsModel->index(i, 0);
        if (m_comicsModel->data(index, ComicItem::IdRole) == m_currentComicId) {
            return index;
        }
    }
    return QModelIndex();
}

QString AppController::comicImageSource(int page)
{
    QNetworkRequest req = apiFactory().createRequest(QString("library/%1/comic/%2/page/%3/remote")
                                                            .arg(m_currentLibraryId)
                                                            .arg(m_currentComicId)
                                                            .arg(page));



    return req.url().toString();
}


// ---- 删除 ------------------------------------------------------------------
bool AppController::deleteServiceConfigured() const
{
    return qstrlen(PINEAPPLE_DELETE_TOKEN) > 0;
}

// 删除服务跑在电脑上，和 YACReader 同一台主机，端口固定 8894。
// 这样不用让用户额外配置地址，只要 YACReader 地址填对了就能推导出来。
void AppController::requestDelete(const QString &comicId)
{
    if (comicId.isEmpty()) {
        emit deleteFinished(false, QStringLiteral("没有选中的漫画"));
        return;
    }
    if (!deleteServiceConfigured()) {
        emit deleteFinished(false, QStringLiteral("未配置删除服务 token，请重新构建"));
        return;
    }

    const QUrl base = m_requestFactory.baseUrl();
    if (!base.isValid() || base.host().isEmpty()) {
        emit deleteFinished(false, QStringLiteral("还没连上服务器"));
        return;
    }

    QUrl url;
    url.setScheme(base.scheme().isEmpty() ? QStringLiteral("http") : base.scheme());
    url.setHost(base.host());
    url.setPort(PINEAPPLE_DELETE_PORT);
    url.setPath(QStringLiteral("/delete"));

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("X-Delete-Token", PINEAPPLE_DELETE_TOKEN);
    req.setTransferTimeout(std::chrono::seconds(60));

    QJsonObject body;
    body.insert(QStringLiteral("comic_id"), comicId.toInt());
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkReply *reply = m_networkAccessManager->post(req, payload);
    connect(reply, &QNetworkReply::finished, this, [this, reply, comicId]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            const QString err = QStringLiteral("删除服务不可达：%1").arg(reply->errorString());
            emit deleteFinished(false, err);
            return;
        }

        const QByteArray raw = reply->readAll();
        const QJsonObject obj = QJsonDocument::fromJson(raw).object();

        if (obj.value(QStringLiteral("ok")).toBool()) {
            const QString name = obj.value(QStringLiteral("name")).toString();
            const QString moved = obj.value(QStringLiteral("moved_to")).toString();

            // 从本地记录里一并清掉这本
            markFinished(comicId, false);
            if (m_favorites.contains(comicId)) {
                m_favorites.remove(comicId);
                QSettings s;
                s.setValue(QStringLiteral("favorites"), QStringList(m_favorites.values()));
                m_favoriteCount = m_favorites.size();
                emit favoriteCountChanged();
            }
            QSettings prog;
            prog.remove(QStringLiteral("progress/%1").arg(comicId));

            // 让列表重新拉一次，服务端重扫后这本就没了
            updateComicsInFolder();

            emit deleteFinished(true,
                QStringLiteral("已删除「%1」\n文件已移到 .deleted 目录，可恢复\n%2")
                    .arg(name, moved));
        } else {
            const QString err = obj.value(QStringLiteral("error")).toString();
            emit deleteFinished(false, err.isEmpty() ? QStringLiteral("删除失败") : err);
        }
    });
}

QNetworkRequestFactory AppController::apiFactory() const
{
    return m_requestFactory;
}