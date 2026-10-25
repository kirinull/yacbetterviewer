#pragma once

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

    // 搜索筛选：在已拉取的列表上本地过滤
    Q_INVOKABLE void setFilter(const QString &text);

    // 排序：在本地对已拉取列表排序
    Q_INVOKABLE void setSort(int mode, bool ascending);
    Q_INVOKABLE void resort();

    // 已读归档：只写本地标记，不动磁盘上的文件
    Q_INVOKABLE void markFinished(const QString &comicId, bool finished);
    Q_INVOKABLE bool isFinished(const QString &comicId) const;
    Q_INVOKABLE void setShowFinished(bool show);

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
    void totalComicCountChanged();
    void sortModeChanged();
    void sortAscendingChanged();
    void showFinishedChanged();
    void finishedCountChanged();

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
};
