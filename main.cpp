#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlNetworkAccessManagerFactory>
#include <QNetworkAccessManager>
#include <QNetworkRequest>

#include "dataitems/libraryitem.h"
#include "dataitems/comicitem.h"
#include "dataitems/folderitem.h"
#include "coverimageprovider.h"

class QmlNetworkAccessManager : public QNetworkAccessManager
{
public:
    QmlNetworkAccessManager(QObject *parent = nullptr) : QNetworkAccessManager(parent) {}

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoingData = nullptr) override
    {
        QNetworkRequest newRequest(request);
        newRequest.setRawHeader("X-Request-Id", "114514");
        return QNetworkAccessManager::createRequest(op, newRequest, outgoingData);
    }
};

class QmlNetworkAccessManagerFactory : public QQmlNetworkAccessManagerFactory
{
public:
    inline QNetworkAccessManager *create(QObject *parent) override
    {
        return new QmlNetworkAccessManager(parent);
    }
};

int main(int argc, char *argv[])
{
    QCoreApplication::setApplicationName("Pineapple Comic Reader");
    QGuiApplication app(argc, argv);
    app.setOrganizationName("Chestnut Software");
    app.setOrganizationDomain("blumia.net");

    // FluentWinUI3 是 Windows 专属的 Qt Quick Controls 样式，Android 上没有这个模块，
    // 强制指定会导致 Main.qml 加载失败并 SIGABRT。
#ifndef Q_OS_ANDROID
    qputenv("QT_QUICK_CONTROLS_STYLE", QByteArray("FluentWinUI3"));
#endif

    qmlRegisterUncreatableMetaObject(LibraryItem::staticMetaObject, "net.blumia.pineapple.comic.reader.libraryitem", 1, 0, "LibraryItem", "enum");
    qmlRegisterUncreatableMetaObject(ComicItem::staticMetaObject, "net.blumia.pineapple.comic.reader.comicitem", 1, 0, "ComicItem", "enum");
    qmlRegisterUncreatableMetaObject(FolderItem::staticMetaObject, "net.blumia.pineapple.comic.reader.folderitem", 1, 0, "FolderItem", "enum");

    QmlNetworkAccessManagerFactory namFactory;

    QQmlApplicationEngine engine;
    engine.setNetworkAccessManagerFactory(&namFactory);
    // 封面统一走这个 provider：先本地缓存，没有才下载，避免同一张图取两次
    engine.addImageProvider(QStringLiteral("cover"), new CoverImageProvider(&engine));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("net.blumia.pineapple.comic.reader", "Main");

    return app.exec();
}
