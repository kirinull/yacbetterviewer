#include "coverimageprovider.h"

#include <QDebug>
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

        auto *nam = new QNetworkAccessManager(this);
        QNetworkReply *reply = nam->get(QNetworkRequest(remoteUrl));

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

void CoverImageProvider::noteHit()
{
    s_hits++;
    if ((s_hits + s_misses) % 5 == 0) {
        qDebug().noquote() << "[COVER]" << statsText();
    }
}

void CoverImageProvider::noteMiss()
{
    s_misses++;
    if ((s_hits + s_misses) % 5 == 0) {
        qDebug().noquote() << "[COVER]" << statsText();
    }
}
qint64 CoverImageProvider::hits() { return s_hits; }
qint64 CoverImageProvider::misses() { return s_misses; }
void CoverImageProvider::resetStats() { s_hits = 0; s_misses = 0; s_writes = 0; }

QString CoverImageProvider::cacheDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/covers");
    QDir().mkpath(dir);
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
