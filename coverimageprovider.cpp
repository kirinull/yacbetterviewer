#include "coverimageprovider.h"

#include <QDebug>
#include <QAtomicInt>
#include <chrono>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QQuickTextureFactory>
#include <QStandardPaths>

QUrl CoverImageProvider::s_baseUrl;
int CoverImageProvider::s_libraryId = -1;
qint64 CoverImageProvider::s_hits = 0;
qint64 CoverImageProvider::s_misses = 0;
qint64 CoverImageProvider::s_writes = 0;

namespace {

// 按线程复用一个 QNAM。
// 原来每张封面都 new 一个，等于每次丢掉 DNS 缓存和连接池，
// 2243 张就是 2243 次 TCP 建连。requestImageResponse 跑在取图线程上，
// 一个 QNAM 只能在所属线程使用，所以用 thread_local。
QNetworkAccessManager *sharedNam()
{
    thread_local QNetworkAccessManager *nam = nullptr;
    if (!nam) {
        nam = new QNetworkAccessManager;   // 进程生命周期内不释放
    }
    return nam;
}

constexpr qint64 kCacheLimit = 120LL * 1024 * 1024;   // 120 MB

// 超限时按修改时间从最旧的开始删
void trimCache()
{
    QDir dir(CoverImageProvider::cacheDir());
    QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("*.jpg"),
                                            QDir::Files, QDir::Time | QDir::Reversed);
    qint64 total = 0;
    for (const QFileInfo &fi : files) {
        total += fi.size();
    }
    if (total <= kCacheLimit) {
        return;
    }
    for (const QFileInfo &fi : files) {
        if (total <= kCacheLimit) {
            break;
        }
        total -= fi.size();
        QFile::remove(fi.absoluteFilePath());
    }
}

class CoverImageResponse : public QQuickImageResponse
{
public:
    CoverImageResponse(const QString &hash, const QString &cachedPath, const QUrl &remoteUrl)
        : m_path(cachedPath)
    {
        QFileInfo fi(cachedPath);
        if (fi.exists() && fi.size() > 0) {
            CoverImageProvider::noteHit();
            if (m_image.load(cachedPath)) {
                emit finished();
                return;
            }
            // 文件损坏，当作未命中重新下载
        }

        CoverImageProvider::noteMiss();

        if (!remoteUrl.isValid()) {
            m_error = QStringLiteral("invalid url");
            emit finished();
            return;
        }

        QNetworkRequest req(remoteUrl);
        req.setTransferTimeout(std::chrono::seconds(10));
        QNetworkReply *reply = sharedNam()->get(req);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();

            if (reply->error() != QNetworkReply::NoError) {
                m_error = reply->errorString();
                emit finished();
                return;
            }

            const QByteArray data = reply->readAll();
            if (data.isEmpty()) {
                m_error = QStringLiteral("empty body");
                emit finished();
                return;
            }

            if (!m_image.loadFromData(data)) {
                m_error = QStringLiteral("decode failed");
                emit finished();
                return;
            }

            // 先写 .part 再改名，避免中途被杀进程留下半个文件
            const QString tmp = m_path + QStringLiteral(".part");
            QFile f(tmp);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(data);
                f.close();
                QFile::remove(m_path);
                QFile::rename(tmp, m_path);

                // trimCache 之前定义了但从未被调用，120MB 上限等于不存在。
                // 它自己也要扫一遍目录，所以每 64 次写入才触发一次。
                static QAtomicInt s_writeCount;
                if ((s_writeCount.fetchAndAddRelaxed(1) & 63) == 0) {
                    trimCache();
                }

            }

            emit finished();
        });
    }

    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }
private:
    QString m_path;
    QImage m_image;
    QString m_error;
};

} // namespace

CoverImageProvider::CoverImageProvider(QObject *parent)
    : QQuickAsyncImageProvider()
{
    Q_UNUSED(parent)
}

void CoverImageProvider::setBaseUrl(const QUrl &url) { s_baseUrl = url; }
void CoverImageProvider::setLibraryId(int id) { s_libraryId = id; }
QUrl CoverImageProvider::baseUrl() { return s_baseUrl; }
int CoverImageProvider::libraryId() { return s_libraryId; }

// 这里只加计数。
// 之前每 5 次请求调一次 statsText()，而它要 entryInfoList 遍历整个缓存目录
// （2243 个文件各 stat 一次），且跑在唯一的取图线程上，滑一屏封面就串行扫几次。
// statsText() 保留给 AppController::coverCacheStats() 手动查看。
void CoverImageProvider::noteHit()
{
    ++s_hits;
}

void CoverImageProvider::noteMiss()
{
    ++s_misses;
}
qint64 CoverImageProvider::hits() { return s_hits; }
qint64 CoverImageProvider::misses() { return s_misses; }
void CoverImageProvider::resetStats() { s_hits = 0; s_misses = 0; s_writes = 0; }

QString CoverImageProvider::cacheDir()
{
    // 静态局部初始化，线程安全，只 mkpath 一次。
    // 原来每张封面都要 mkpath（每个路径分量一次 stat）。
    static const QString dir = [] {
        const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                          + QStringLiteral("/covers");
        QDir().mkpath(d);
        return d;
    }();
    return dir;
}

QString CoverImageProvider::statsText()
{
    const qint64 total = s_hits + s_misses;
    const double rate = total > 0 ? (s_hits * 100.0 / total) : 0.0;

    QDir dir(cacheDir());
    const QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("*.jpg"), QDir::Files);
    qint64 bytes = 0;
    for (const QFileInfo &fi : files) {
        bytes += fi.size();
    }

    return QStringLiteral("命中 %1 / 未命中 %2 / 命中率 %3% / 缓存 %4 张 %5 MB")
        .arg(s_hits)
        .arg(s_misses)
        .arg(QString::number(rate, 'f', 1))
        .arg(files.size())
        .arg(QString::number(bytes / 1048576.0, 'f', 1));
}

QQuickImageResponse *CoverImageProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    Q_UNUSED(requestedSize)

    // id 形如 <hash>，可能带查询串（QML 常用 ?v=x 强制刷新），截掉
    QString hash = id;
    const int q = hash.indexOf(QLatin1Char('?'));
    if (q >= 0) {
        hash = hash.left(q);
    }

    if (hash.isEmpty() || s_libraryId < 0 || !s_baseUrl.isValid()) {
        return new CoverImageResponse(hash, QString(), QUrl());
    }

    QUrl remote = s_baseUrl;
    remote.setPath(remote.path() + QStringLiteral("library/") + QString::number(s_libraryId)
                   + QStringLiteral("/cover/") + hash + QStringLiteral(".jpg"));

    const QString cached = cacheDir() + QLatin1Char('/') + hash + QStringLiteral(".jpg");

    return new CoverImageResponse(hash, cached, remote);
}
