#pragma once

#include <QImage>
#include <QJsonArray>
#include <QNetworkRequestFactory>
#include <QQmlEngine>
#include <QRestAccessManager>
#include <QStandardItemModel>

class AppController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    enum ConnectionState {
        NotConnected,
        Connecting,
        Connected
    };
    Q_ENUM(ConnectionState)

    Q_PROPERTY(ConnectionState connectionState MEMBER m_connectionState NOTIFY connectionStateChanged FINAL)
    Q_PROPERTY(int selectedLibraryId MEMBER m_currentLibraryId NOTIFY currentLibraryIdChanged FINAL)
    Q_PROPERTY(QString selectedComicId MEMBER m_currentComicId NOTIFY currentComicIdChanged FINAL)
    Q_PROPERTY(bool selectedComicOpened MEMBER m_currentComicOpened NOTIFY currentComicOpenedChanged FINAL)
    Q_PROPERTY(QString lastErrorMessage MEMBER m_lastErrorMessage NOTIFY lastErrorMessageChanged FINAL)
    Q_PROPERTY(QStandardItemModel * librariesModel MEMBER m_librariesModel CONSTANT FINAL)
    Q_PROPERTY(QStandardItemModel * comicsModel MEMBER m_comicsModel CONSTANT FINAL)
    Q_PROPERTY(QStandardItemModel * foldersModel MEMBER m_foldersModel CONSTANT FINAL)
    Q_PROPERTY(QString filterText MEMBER m_filterText NOTIFY filterTextChanged FINAL)
    Q_PROPERTY(int filteredCount MEMBER m_filteredCount NOTIFY filteredCountChanged FINAL)
    Q_PROPERTY(int totalComicCount MEMBER m_totalComicCount NOTIFY totalComicCountChanged FINAL)
    Q_PROPERTY(int sortMode MEMBER m_sortMode NOTIFY sortModeChanged FINAL)
    Q_PROPERTY(bool sortAscending MEMBER m_sortAscending NOTIFY sortAscendingChanged FINAL)
    Q_PROPERTY(bool showFinished MEMBER m_showFinished NOTIFY showFinishedChanged FINAL)
    Q_PROPERTY(int finishedCount MEMBER m_finishedCount NOTIFY finishedCountChanged FINAL)
    Q_PROPERTY(bool showFavorites MEMBER m_showFavorites NOTIFY showFavoritesChanged FINAL)
    Q_PROPERTY(int favoriteCount MEMBER m_favoriteCount NOTIFY favoriteCountChanged FINAL)
    Q_PROPERTY(QString authorFilter MEMBER m_authorFilter NOTIFY authorFilterChanged FINAL)
    Q_PROPERTY(QString currentAuthor READ currentAuthor NOTIFY currentComicIdChanged FINAL)
    // 列表滚动位置：从详情返回时用来回到原来那一行
    Q_PROPERTY(double gridScrollY MEMBER m_gridScrollY NOTIFY gridScrollYChanged FINAL)

    // 排序方式，与 QML 下拉菜单的 index 对应
    enum SortMode {
        SortByAdded = 0,
        SortByName,
        SortByAuthor,
        SortBySize,
        SortByPages,
        SortRandom
    };
    Q_ENUM(SortMode)

    AppController(QObject *parent = nullptr);

    Q_INVOKABLE void connectServer(QUrl serverBaseUrl);
    Q_INVOKABLE void updateLibraries();
    Q_INVOKABLE void updateComicsInFolder(QString folderId = "1");
    Q_INVOKABLE void openComic();
    Q_INVOKABLE void closeComic();
    Q_INVOKABLE QString coverImageSource(QString comicHash);
    Q_INVOKABLE QModelIndex currentComicModelIndex();
    Q_INVOKABLE QString comicImageSource(int page);

    // 删除本子：调用电脑上的删除服务。文件不直接销毁，
    // 服务端会移到库目录的 .deleted\ 下保留可恢复。
    Q_INVOKABLE void requestDelete(const QString &comicId);
    Q_INVOKABLE bool deleteServiceConfigured() const;

    // 供 QQuickImageProvider 调用：同步取页图（带 X-Request-Id，命中缓存则直接读文件）。
    // QML 的 Image 走 Qt 全局网络栈，带不上请求头，只能绕到 C++ 这边取。
    QImage loadPageImage(int page);
    QString pageCachePath(int page) const;

    // 搜索筛选：在已拉取的列表上本地过滤
    Q_INVOKABLE void setFilter(const QString &text);

    // 排序：在本地对已拉取列表排序
    Q_INVOKABLE void setSort(int mode, bool ascending);
    Q_INVOKABLE void resort();

    // 已读归档：只写本地标记，不动磁盘上的文件
    Q_INVOKABLE void markFinished(const QString &comicId, bool finished);
    Q_INVOKABLE bool isFinished(const QString &comicId) const;
    Q_INVOKABLE void setShowFinished(bool show);

    // 收藏：本地标记，服务端没有写收藏的 API
    Q_INVOKABLE void toggleFavorite(const QString &comicId);
    Q_INVOKABLE bool isFavorite(const QString &comicId) const;
    Q_INVOKABLE void setShowFavorites(bool show);

    // 作者筛选：点详情页的作者名进入该作者的全部作品
    Q_INVOKABLE void filterByAuthor(const QString &author);
    Q_INVOKABLE void clearAuthorFilter();
    QString currentAuthor() const;

    // 下一本：优先同作者，同作者没了就跳到下一个作者
    Q_INVOKABLE bool hasNextComic() const;
    Q_INVOKABLE void openNextComic();

    // 阅读进度：本地持久化，服务端 current_page 作为首次打开时的兜底
    Q_INVOKABLE int savedProgress(const QString &comicId) const;
    Q_INVOKABLE void saveProgress(const QString &comicId, int page);
    Q_INVOKABLE int serverProgress() const;

signals:
    void librariesLoaded(int count);
    void connectionStateChanged(ConnectionState newState);
    void currentLibraryIdChanged(int newLibraryId);
    void currentComicIdChanged(QString newComicId);
    void currentComicOpenedChanged(bool opened);
    void lastErrorMessageChanged(QString errorMessage);
    void filterTextChanged();
    void filteredCountChanged();
    void deleteFinished(bool ok, const QString &message);
    void totalComicCountChanged();
    void sortModeChanged();
    void sortAscendingChanged();
    void showFinishedChanged();
    void finishedCountChanged();
    void showFavoritesChanged();
    void favoriteCountChanged();
    void authorFilterChanged();
    void gridScrollYChanged();
    void comicsReloaded();

private:
    QNetworkRequestFactory apiFactory() const;
    void repopulateComicsModel();
    static QString extractAuthor(const QString &fileName);

    ConnectionState m_connectionState = NotConnected;
    int m_currentLibraryId = -1;
    QString m_currentComicId;
    bool m_currentComicOpened = false;
    QString m_lastErrorMessage;
    QNetworkRequestFactory m_requestFactory;
    QNetworkAccessManager * m_networkAccessManager;
    QRestAccessManager * m_restAccessManager;
    QStandardItemModel * m_librariesModel;
    QStandardItemModel * m_comicsModel;
    QStandardItemModel * m_foldersModel;

    QJsonArray m_allComics;      // 全量，用于重新过滤
    QString m_filterText;
    int m_filteredCount = 0;
    int m_totalComicCount = 0;
    int m_serverProgress = 0;    // 当前选中漫画的服务端进度
    int m_sortMode = SortByAdded;
    bool m_sortAscending = false;   // 默认最新添加在前
    QHash<QString, int> m_randomRank;  // 随机排序的稳定映射
    bool m_showFinished = false;    // 当前是否在“已看过”视图
    int m_finishedCount = 0;        // 已归档数量（只用于显示）
    QSet<QString> m_finished;       // 已读完的 comicId 集合（本地标记）
    bool m_switchingComic = false;  // “下一本”切换中，不要关阅读器
    bool m_showFavorites = false;   // 当前是否在“收藏”视图
    int m_favoriteCount = 0;
    QSet<QString> m_favorites;      // 收藏的 comicId 集合（本地标记）
    QString m_authorFilter;         // 按作者筛选，空表示不筛
    double m_gridScrollY = 0;       // 列表滚动位置（像素）
    QString m_pageCacheDir;         // 页面图片缓存目录
};
