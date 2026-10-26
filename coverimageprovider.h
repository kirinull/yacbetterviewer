#pragma once

#include <QQuickAsyncImageProvider>
#include <QQuickImageResponse>
#include <QUrl>
#include <QString>

// 封面图提供器。
//
// 之前 QML 的 Image 直连网络地址，导致同一张封面要下载两次（Image 自己
// 拉一次，后台缓存再存一次）。交给 ImageProvider 后取图只有一条路径：
// 先看本地缓存，没有就下载并落盘，一次请求对应一次下载。
//
// QML 侧只写 image://cover/<hash>。
class CoverImageProvider : public QQuickAsyncImageProvider
{
public:
    explicit CoverImageProvider(QObject *parent = nullptr);

    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

    // 由 AppController 在连接成功后写入，用来拼封面 URL。
    static void setBaseUrl(const QUrl &url);
    static void setLibraryId(int id);
    static QUrl baseUrl();
    static int libraryId();
    static QString cacheDir();

    // 命中率统计，口径为请求数
    static void noteHit();
    static void noteMiss();
    static qint64 hits();
    static qint64 misses();
    static void resetStats();
    static QString statsText();

private:
    static QUrl s_baseUrl;
    static int s_libraryId;
    static qint64 s_hits;
    static qint64 s_misses;
    static qint64 s_writes;
};
