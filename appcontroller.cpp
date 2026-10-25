#include "appcontroller.h"

#include "dataitems/comicitem.h"
#include "dataitems/folderitem.h"
#include "dataitems/libraryitem.h"

#include <chrono>
#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QRestAccessManager>
#include <QRestReply>
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
    connect(this, &AppController::currentComicIdChanged, this, [this](){
        // "selected" is "current"
        setProperty("selectedComicOpened", false);
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

    // 先过滤，再排序
    QList<QJsonObject> rows;
    rows.reserve(m_allComics.size());
    for (const QJsonValue & value : std::as_const(m_allComics)) {
        const QJsonObject obj = value.toObject();
        if (!needle.isEmpty()
            && !obj["file_name"].toString().contains(needle, Qt::CaseInsensitive)) {
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

QNetworkRequestFactory AppController::apiFactory() const
{
    return m_requestFactory;
}