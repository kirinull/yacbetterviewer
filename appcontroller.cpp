#include "appcontroller.h"

#include "dataitems/comicitem.h"
#include "dataitems/folderitem.h"
#include "dataitems/libraryitem.h"

#include <chrono>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
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
    int shown = 0;

    for (const QJsonValue & value : std::as_const(m_allComics)) {
        const QJsonObject obj = value.toObject();
        const QString name = obj["file_name"].toString();

        if (!needle.isEmpty() && !name.contains(needle, Qt::CaseInsensitive)) {
            continue;
        }

        m_comicsModel->appendRow(new ComicItem(obj, name));
        ++shown;
    }

    if (m_filteredCount != shown) {
        m_filteredCount = shown;
        emit filteredCountChanged();
    }
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

    m_restAccessManager->get(apiFactory().createRequest(QString("library/%1/comic/%2/remote")
                                                            .arg(m_currentLibraryId).arg(m_currentComicId)),
                            this, [=](QRestReply &reply){
        if (reply.isSuccess()) {
            setProperty("selectedComicOpened", true);
        } else {
            setProperty("selectedComicOpened", false);
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
