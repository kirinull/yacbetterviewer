#include "appcontroller.h"
#include "coverimageprovider.h"

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
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

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

    // 恢复上次的列表位置、列数与浏览历史
    m_columnCount = qBound(1, settings.value(QStringLiteral("grid/columns"), 2).toInt(), 4);
    m_gridFirstIndex = settings.value(QStringLiteral("grid/firstIndex"), -1).toInt();
    m_history = settings.value(QStringLiteral("history")).toStringList();

    // 恢复上次的排序方式。排序不变，记下的行号才有意义；
    // 特别是随机排序，靠持久化的 seed 才能重启后维持同一顺序。
    m_sortMode = settings.value(QStringLiteral("sort/mode"), 0).toInt();
    m_sortAscending = settings.value(QStringLiteral("sort/ascending"), false).toBool();

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
                // 无条件通知：列表已重填。QML 靠它恢复滚动位置。
                // 不能用 filteredCountChanged，那个只在数量变化时才发，
                // 页面重建后数量往往一样，信号根本不触发。
                qDebug() << "[SCROLL] 列表重填完成，应恢复位置" << m_gridScrollY;
                emit comicsReloaded();
            }
        }
    });
}

void AppController::repopulateComicsModel()
{
    m_comicsModel->clear();

    // 随机排序的权重靠持久化 seed 重建。启动后第一次填充时这表是空的，
    // 不补就会退回原始顺序，重启后看起来就像「随机没记住」。
    if (m_sortMode == SortRandom && m_randomRank.isEmpty() && !m_allComics.isEmpty()) {
        QSettings settings;
        quint32 seed = settings.value(QStringLiteral("sort/randomSeed"), 0).toUInt();
        if (seed == 0) {
            seed = QRandomGenerator::global()->generate();
            settings.setValue(QStringLiteral("sort/randomSeed"), seed);
        }
        QRandomGenerator rng(seed);
        for (const QJsonValue &value : std::as_const(m_allComics)) {
            m_randomRank.insert(value.toObject()["id"].toString(), rng.generate());
        }
    }

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
        // 作者筛选：只看这个作者的作品
        if (!m_authorFilter.isEmpty()) {
            if (extractAuthor(obj["file_name"].toString()) != m_authorFilter) {
                continue;
            }
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
    case SortByCircle:
        std::sort(rows.begin(), rows.end(), [&](const QJsonObject &a, const QJsonObject &b) {
            // 没有社团信息的排到最后，避免混进字母序列中间
            const QString ca = extractCircle(a["file_name"].toString());
            const QString cb = extractCircle(b["file_name"].toString());
            if (ca.isEmpty() != cb.isEmpty()) {
                return !ca.isEmpty();
            }
            const int cmp = QString::localeAwareCompare(ca, cb);
            if (cmp != 0) return m_sortAscending ? cmp < 0 : cmp > 0;
            const int nc = QString::localeAwareCompare(a["file_name"].toString(),
                                                       b["file_name"].toString());
            return m_sortAscending ? nc < 0 : nc > 0;
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
// 从文件名里提取作者与社团。
//
// 同人本常见格式是 "[社团 (作者)] 标题"，也有 "[作者] 标题"。
// 之前这里取的是括号前的内容，等于把社团当成了作者，
// 导致 [遥弤社 (源五郎)] 被归到「遥弤社」下。
// 现在：有括号取括号内的作者，无括号则整体当作作者。
QString AppController::extractAuthor(const QString &fileName)
{
    const int lb = fileName.indexOf(QLatin1Char('['));
    if (lb >= 0) {
        const int rb = fileName.indexOf(QLatin1Char(']'), lb);
        if (rb > lb) {
            const QString inner = fileName.mid(lb + 1, rb - lb - 1).trimmed();

            const int lparen = inner.indexOf(QLatin1Char('('));
            const int rparen = inner.lastIndexOf(QLatin1Char(')'));
            if (lparen >= 0 && rparen > lparen) {
                // "[社团 (作者)]" -> 取作者
                const QString author = inner.mid(lparen + 1, rparen - lparen - 1).trimmed();
                if (!author.isEmpty()) {
                    return author;
                }
            }
            // 没有括号，或括号内为空，整体当作作者
            return inner;
        }
    }
    const int sp = fileName.indexOf(QLatin1Char(' '));
    return (sp > 0 ? fileName.left(sp) : fileName).trimmed();
}

// 提取社团名。"[社团 (作者)]" -> 社团；"[作者]" -> 空（无社团信息）。
QString AppController::extractCircle(const QString &fileName)
{
    const int lb = fileName.indexOf(QLatin1Char('['));
    if (lb >= 0) {
        const int rb = fileName.indexOf(QLatin1Char(']'), lb);
        if (rb > lb) {
            const QString inner = fileName.mid(lb + 1, rb - lb - 1).trimmed();
            const int lparen = inner.indexOf(QLatin1Char('('));
            const int rparen = inner.lastIndexOf(QLatin1Char(')'));
            if (lparen > 0 && rparen > lparen) {
                return inner.left(lparen).trimmed();
            }
        }
    }
    return QString();
}

void AppController::setSort(int mode, bool ascending)
{
    m_sortMode = mode;
    m_sortAscending = ascending;

    // 排序方式持久化：关掉再开还是上次那个排法，
    // 否则列表一重排，记下的行号就指错地方了。
    QSettings settings;
    settings.setValue(QStringLiteral("sort/mode"), mode);
    settings.setValue(QStringLiteral("sort/ascending"), ascending);

    // 重新排列后旧行号失去意义，归零回顶部。
    // 这是唯一该回顶部的场景；点开漫画返回、重启 App 都应该停在原处。
    resetListPosition();

    emit sortModeChanged();
    emit sortAscendingChanged();

    if (mode == SortRandom) {
        rebuildRandomRanks(false);
    } else {
        repopulateComicsModel();
    }
}

void AppController::resort()
{
    rebuildRandomRanks(false);
}

// 重建随机权重。
// forceNewSeed = true 表示用户主动点了「随机洗牌」，换一批顺序；
// false 表示启动或刷新时重建，要用上次存下的 seed，
// 否则每次重启随机顺序全变，记下的位置完全没意义。
void AppController::rebuildRandomRanks(bool forceNewSeed)
{
    QSettings settings;
    quint32 seed = 0;
    if (!forceNewSeed) {
        seed = settings.value(QStringLiteral("sort/randomSeed"), 0).toUInt();
    }
    if (seed == 0) {
        seed = QRandomGenerator::global()->generate();
        settings.setValue(QStringLiteral("sort/randomSeed"), seed);
    }

    QRandomGenerator rng(seed);
    m_randomRank.clear();
    for (const QJsonValue &value : std::as_const(m_allComics)) {
        m_randomRank.insert(value.toObject()["id"].toString(), rng.generate());
    }
    repopulateComicsModel();
}

void AppController::reshuffleRandom()
{
    // 换了一批顺序，同样归零回顶部
    resetListPosition();
    rebuildRandomRanks(true);
}

// 把列表位置归零。调用方：切换排序、随机洗牌、切换列数。
void AppController::resetListPosition()
{
    if (m_gridFirstIndex == 0 && qFuzzyIsNull(m_gridScrollY)) {
        return;
    }
    m_gridFirstIndex = 0;
    m_gridScrollY = 0;
    QSettings settings;
    settings.setValue(QStringLiteral("grid/firstIndex"), 0);
    emit gridFirstIndexChanged();
    emit gridScrollYChanged();
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

    // 记入浏览历史（最新的在前，去重，上限 100）
    if (!m_currentComicId.isEmpty()) {
        m_history.removeAll(m_currentComicId);
        m_history.prepend(m_currentComicId);
        while (m_history.size() > 100) {
            m_history.removeLast();
        }
        QSettings settings;
        settings.setValue(QStringLiteral("history"), m_history);
        emit historyChanged();
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
    if (m_currentLibraryId < 0 || comicHash.isEmpty()) {
        return QString();
    }

    // 封面统一交给 CoverImageProvider：它内部先查本地缓存，
    // 未命中才下载并落盘，一次请求一次下载。
    CoverImageProvider::setBaseUrl(apiFactory().baseUrl());
    CoverImageProvider::setLibraryId(m_currentLibraryId);

    return QStringLiteral("image://cover/") + comicHash;
}

QString AppController::coverCacheStats() const
{
    return CoverImageProvider::statsText();
}

void AppController::resetCoverCacheStats()
{
    CoverImageProvider::resetStats();
}

void AppController::clearCoverCache()
{
    QDir dir(CoverImageProvider::cacheDir());
    const QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("*.jpg")
                                                            << QStringLiteral("*.part"),
                                                 QDir::Files);
    for (const QFileInfo &fi : files) {
        QFile::remove(fi.absoluteFilePath());
    }
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


// 当前选中漫画的作者名，详情页用来显示并支持点进去
double AppController::gridScrollY() const
{
    return m_gridScrollY;
}

void AppController::setGridScrollY(double y)
{
    if (qFuzzyCompare(m_gridScrollY + 1.0, y + 1.0)) {
        return;
    }
    m_gridScrollY = y;
    emit gridScrollYChanged();
}

int AppController::gridFirstIndex() const
{
    return m_gridFirstIndex;
}

// 滚动位置写进 QSettings，下次启动 App 直接回到上次那一行。
// 滚动时这个 setter 会被高频调用，直接写盘会拖慢滚动，
// 所以内存里立即更新，落盘用一个 800ms 的单次定时器节流。
void AppController::setGridFirstIndex(int index)
{
    if (m_gridFirstIndex == index) {
        return;
    }
    m_gridFirstIndex = index;
    emit gridFirstIndexChanged();

    if (!m_scrollSaveTimer) {
        m_scrollSaveTimer = new QTimer(this);
        m_scrollSaveTimer->setSingleShot(true);
        m_scrollSaveTimer->setInterval(800);
        connect(m_scrollSaveTimer, &QTimer::timeout, this, [this]() {
            QSettings settings;
            settings.setValue(QStringLiteral("grid/firstIndex"), m_gridFirstIndex);
        });
    }
    m_scrollSaveTimer->start();
}

int AppController::columnCount() const
{
    return m_columnCount;
}

void AppController::setColumnCount(int count)
{
    count = qBound(1, count, 4);
    if (m_columnCount == count) {
        return;
    }
    m_columnCount = count;
    QSettings settings;
    settings.setValue(QStringLiteral("grid/columns"), count);
    // 列数变了，同一个行号对应的画面完全不同，归零
    resetListPosition();
    emit columnCountChanged();
}

QVariantList AppController::historyList() const
{
    QVariantList out;
    for (const QString &comicId : m_history) {
        for (const QJsonValue &value : std::as_const(m_allComics)) {
            const QJsonObject obj = value.toObject();
            if (obj["id"].toString() != comicId) {
                continue;
            }
            QVariantMap item;
            item[QStringLiteral("comicId")] = comicId;
            item[QStringLiteral("title")] = obj["file_name"].toString();
            item[QStringLiteral("hash")] = obj["hash"].toString();
            item[QStringLiteral("pageCount")] = obj["num_pages"].toInt();
            item[QStringLiteral("currentPage")] = savedProgress(comicId);
            out.append(item);
            break;
        }
    }
    return out;
}

void AppController::clearHistory()
{
    m_history.clear();
    QSettings settings;
    settings.remove(QStringLiteral("history"));
    emit historyChanged();
}

void AppController::openComicFromHistory(const QString &comicId)
{
    // 历史里的书可能被「已看过」或搜索过滤掉，先清过滤再定位，
    // 否则详情页拿不到索引，进去是空的
    clearAuthorFilter();
    setShowFinished(false);
    setShowFavorites(false);
    setFilter(QString());

    m_currentComicId = comicId;
    emit currentComicIdChanged(comicId);
}

QString AppController::currentAuthor() const
{
    if (m_currentComicId.isEmpty()) {
        return QString();
    }
    for (const QJsonValue &value : std::as_const(m_allComics)) {
        const QJsonObject obj = value.toObject();
        if (obj["id"].toString() == m_currentComicId) {
            return extractAuthor(obj["file_name"].toString());
        }
    }
    return QString();
}

void AppController::filterByAuthor(const QString &author)
{
    if (author.isEmpty()) {
        return;
    }
    m_authorFilter = author;
    emit authorFilterChanged();
    // 作者视图里不叠加“已看过/收藏”这些状态筛选，否则容易只看到空列表
    m_showFinished = false;
    m_showFavorites = false;
    emit showFinishedChanged();
    emit showFavoritesChanged();
    // 回到列表
    setProperty("selectedComicId", "");
    repopulateComicsModel();
}

QString AppController::currentCircle() const
{
    for (const QJsonValue &value : std::as_const(m_allComics)) {
        const QJsonObject obj = value.toObject();
        if (obj["id"].toString() == m_currentComicId) {
            return extractCircle(obj["file_name"].toString());
        }
    }
    return QString();
}

void AppController::filterByCircle(const QString &circle)
{
    if (circle.isEmpty()) {
        return;
    }
    // 复用作者筛选的展示逻辑：按社团名过滤列表
    m_authorFilter = circle;
    emit authorFilterChanged();
    m_showFinished = false;
    m_showFavorites = false;
    emit showFinishedChanged();
    emit showFavoritesChanged();
    setProperty("selectedComicId", "");
    repopulateComicsModel();
}

void AppController::clearAuthorFilter()
{
    if (m_authorFilter.isEmpty()) {
        return;
    }
    m_authorFilter.clear();
    emit authorFilterChanged();
    repopulateComicsModel();
}

QNetworkRequestFactory AppController::apiFactory() const
{
    return m_requestFactory;
}