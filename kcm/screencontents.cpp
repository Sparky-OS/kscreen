/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "screencontents.h"

#include "kcm_screen_debug.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QDBusVariant>
#include <QFutureWatcher>
#include <QPainter>
#include <QPromise>
#include <QThreadPool>

#include <cerrno>
#include <memory>
#include <utility>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

static const QString s_screenShotService = u"org.kde.KWin.ScreenShot2"_s;
static const QString s_screenShotPath = u"/org/kde/KWin/ScreenShot2"_s;
static const QString s_screenShotInterface = u"org.kde.KWin.ScreenShot2"_s;

static const QString s_upowerService = u"org.freedesktop.UPower"_s;
static const QString s_upowerPath = u"/org/freedesktop/UPower"_s;
static const QString s_propertiesInterface = u"org.freedesktop.DBus.Properties"_s;

static const QString s_configFile = u"kcm_kscreenrc"_s;
static const QString s_configGroup = u"ArrangementView"_s;
static const QString s_configKey = u"ShowScreenContents"_s;

// Reads the raw image KWin writes into the pipe after its reply, and makes the preview from it
static QImage readPreview(int fd, const QVariantMap &metadata)
{
    const uint width = metadata.value(u"width"_s).toUInt();
    const uint height = metadata.value(u"height"_s).toUInt();
    const uint stride = metadata.value(u"stride"_s).toUInt();
    const auto format = QImage::Format(metadata.value(u"format"_s).toUInt());
    if (metadata.value(u"type"_s).toString() != u"raw"_s || width == 0 || height == 0 || format <= QImage::Format_Invalid
        || format >= QImage::NImageFormats || stride < width) {
        close(fd);
        return {};
    }

    QByteArray buffer(qsizetype(stride) * height, Qt::Uninitialized);
    qsizetype received = 0;
    pollfd pfd{fd, POLLIN, 0};
    while (received < buffer.size()) {
        const int ready = poll(&pfd, 1, 10000);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready <= 0) {
            break;
        }
        const ssize_t count = read(fd, buffer.data() + received, buffer.size() - received);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        received += count;
    }
    close(fd);
    if (received != buffer.size()) {
        return {};
    }

    QImage image(reinterpret_cast<const uchar *>(buffer.constData()), width, height, stride, format);
    // Stereo KWin hands a screen in a 3D mode over as both eyes side by side, the left eye first;
    // the arrangement view is flat, so its preview is the left eye
    if (metadata.value(u"stereo"_s).toBool() && width >= 2) {
        image = image.copy(0, 0, width / 2, height);
    }
    // A screen is opaque: where nothing is drawn, KWin's capture is transparent and the screen black
    return image.convertToFormat(QImage::Format_RGB32)
        .scaled(QSize(ScreenContents::maximumPreviewSize, ScreenContents::maximumPreviewSize), Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

ScreenContents::ScreenContents(QObject *parent)
    : QObject(parent)
{
    const KConfigGroup group = KSharedConfig::openConfig(s_configFile)->group(s_configGroup);
    if (group.hasKey(s_configKey)) {
        m_choice = group.readEntry(s_configKey, true);
    }

    m_timer.setInterval(captureInterval);
    connect(&m_timer, &QTimer::timeout, this, &ScreenContents::captureAll);

    QDBusConnection::systemBus().connect(s_upowerService,
                                         s_upowerPath,
                                         s_propertiesInterface,
                                         u"PropertiesChanged"_s,
                                         this,
                                         SLOT(upowerPropertiesChanged(QString, QVariantMap, QStringList)));
    readOnBattery();
}

ScreenContents::~ScreenContents() = default;

bool ScreenContents::isShown() const
{
    return m_choice.value_or(!m_onBattery);
}

void ScreenContents::setShown(bool shown)
{
    const bool wasShown = isShown();
    m_choice = shown;
    KConfigGroup group = KSharedConfig::openConfig(s_configFile)->group(s_configGroup);
    group.writeEntry(s_configKey, shown);
    group.sync();
    if (shown) {
        // Ask KWin again, in case it refused before
        setErrorString(QString());
    }
    if (wasShown != shown) {
        Q_EMIT shownChanged();
        updateCapturing();
    }
}

bool ScreenContents::isActive() const
{
    return m_active;
}

void ScreenContents::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    m_active = active;
    Q_EMIT activeChanged();
    updateCapturing();
}

QString ScreenContents::errorString() const
{
    return m_errorString;
}

void ScreenContents::setErrorString(const QString &errorString)
{
    if (m_errorString == errorString) {
        return;
    }
    m_errorString = errorString;
    Q_EMIT errorStringChanged();
    updateCapturing();
}

void ScreenContents::watch(const QString &outputName)
{
    if (outputName.isEmpty()) {
        return;
    }
    if (m_watched[outputName]++ == 0 && m_timer.isActive()) {
        capture(outputName);
    }
}

void ScreenContents::unwatch(const QString &outputName)
{
    auto it = m_watched.find(outputName);
    if (it == m_watched.end()) {
        return;
    }
    if (--it.value() <= 0) {
        m_watched.erase(it);
        m_images.remove(outputName);
    }
}

QImage ScreenContents::image(const QString &outputName) const
{
    return m_images.value(outputName);
}

void ScreenContents::updateCapturing()
{
    const bool capturing = isShown() && m_active && m_errorString.isEmpty();
    if (capturing == m_timer.isActive()) {
        return;
    }
    if (capturing) {
        m_timer.start();
        captureAll();
    } else {
        m_timer.stop();
    }
}

void ScreenContents::captureAll()
{
    const auto names = m_watched.keys();
    for (const QString &name : names) {
        capture(name);
    }
}

void ScreenContents::capture(const QString &outputName)
{
    // One capture per screen at a time; a slow one skips a turn
    if (m_pending.contains(outputName)) {
        return;
    }

    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) {
        qCWarning(KSCREEN_KCM) << "Could not create a pipe for the screen contents:" << strerror(errno);
        return;
    }

    QDBusMessage message = QDBusMessage::createMethodCall(s_screenShotService, s_screenShotPath, s_screenShotInterface, u"CaptureScreen"_s);
    const QVariantMap options{
        {u"include-cursor"_s, false},
        {u"native-resolution"_s, false},
    };
    message << outputName << options << QVariant::fromValue(QDBusUnixFileDescriptor(fds[1]));
    close(fds[1]);
    // The read end is closed with the last lambda that holds it, unless a reader takes it
    std::shared_ptr<int> readFd(new int(fds[0]), [](int *fd) {
        if (*fd >= 0) {
            close(*fd);
        }
        delete fd;
    });

    m_pending.insert(outputName);
    auto callWatcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(callWatcher, &QDBusPendingCallWatcher::finished, this, [this, outputName, readFd](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusPendingReply<QVariantMap> reply = *watcher;
        if (reply.isError()) {
            m_pending.remove(outputName);
            qCWarning(KSCREEN_KCM) << "KWin did not capture" << outputName << reply.error().name() << reply.error().message();
            if (reply.error().name() != u"org.kde.KWin.ScreenShot2.Error.InvalidScreen"_s) {
                setErrorString(reply.error().message());
            }
            return;
        }

        const QVariantMap metadata = reply.value();
        auto promise = std::make_shared<QPromise<QImage>>();
        auto imageWatcher = new QFutureWatcher<QImage>(this);
        connect(imageWatcher, &QFutureWatcherBase::finished, this, [this, imageWatcher, outputName] {
            imageWatcher->deleteLater();
            m_pending.remove(outputName);
            const QImage image = imageWatcher->future().resultCount() > 0 ? imageWatcher->result() : QImage();
            if (image.isNull() || !m_watched.contains(outputName)) {
                return;
            }
            m_images.insert(outputName, image);
            Q_EMIT imageChanged(outputName);
        });
        imageWatcher->setFuture(promise->future());
        promise->start();
        QThreadPool::globalInstance()->start([promise, fd = std::exchange(*readFd, -1), metadata] {
            promise->addResult(readPreview(fd, metadata));
            promise->finish();
        });
    });
}

void ScreenContents::setOnBattery(bool onBattery)
{
    if (m_onBattery == onBattery) {
        return;
    }
    const bool wasShown = isShown();
    m_onBattery = onBattery;
    if (wasShown != isShown()) {
        Q_EMIT shownChanged();
        updateCapturing();
    }
}

void ScreenContents::readOnBattery()
{
    QDBusMessage message = QDBusMessage::createMethodCall(s_upowerService, s_upowerPath, s_propertiesInterface, u"Get"_s);
    message << s_upowerService << u"OnBattery"_s;
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusPendingReply<QDBusVariant> reply = *watcher;
        if (reply.isError()) {
            // Without UPower there is no battery to spare
            qCDebug(KSCREEN_KCM) << "Could not read whether the computer runs on battery:" << reply.error().message();
            return;
        }
        setOnBattery(reply.value().variant().toBool());
    });
}

void ScreenContents::upowerPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
{
    if (interface != s_upowerService) {
        return;
    }
    if (const auto it = changed.constFind(u"OnBattery"_s); it != changed.constEnd()) {
        setOnBattery(it->toBool());
    } else if (invalidated.contains(u"OnBattery"_s)) {
        readOnBattery();
    }
}

ScreenContentsItem::ScreenContentsItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
}

ScreenContentsItem::~ScreenContentsItem()
{
    if (m_source && !m_watchedName.isEmpty()) {
        m_source->unwatch(m_watchedName);
    }
}

ScreenContents *ScreenContentsItem::source() const
{
    return m_source;
}

void ScreenContentsItem::setSource(ScreenContents *source)
{
    if (m_source == source) {
        return;
    }
    if (m_source) {
        if (!m_watchedName.isEmpty()) {
            m_source->unwatch(m_watchedName);
            m_watchedName.clear();
        }
        disconnect(m_source, nullptr, this, nullptr);
    }
    m_source = source;
    if (m_source) {
        connect(m_source, &ScreenContents::imageChanged, this, [this](const QString &outputName) {
            if (outputName == m_outputName) {
                updateImage();
            }
        });
        connect(m_source, &ScreenContents::shownChanged, this, &ScreenContentsItem::updateImage);
    }
    Q_EMIT sourceChanged();
    updateWatching();
    updateImage();
}

QString ScreenContentsItem::outputName() const
{
    return m_outputName;
}

void ScreenContentsItem::setOutputName(const QString &outputName)
{
    if (m_outputName == outputName) {
        return;
    }
    m_outputName = outputName;
    Q_EMIT outputNameChanged();
    updateWatching();
    updateImage();
}

bool ScreenContentsItem::hasImage() const
{
    return !m_image.isNull();
}

void ScreenContentsItem::paint(QPainter *painter)
{
    if (m_image.isNull()) {
        return;
    }
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->drawImage(boundingRect(), m_image);
}

void ScreenContentsItem::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickPaintedItem::itemChange(change, value);
    if (change == ItemVisibleHasChanged || change == ItemSceneChange) {
        updateWatching();
    }
}

void ScreenContentsItem::updateWatching()
{
    // Only the screens the arrangement shows are captured
    const QString name = (m_source && isVisible() && window()) ? m_outputName : QString();
    if (name == m_watchedName) {
        return;
    }
    if (m_source && !m_watchedName.isEmpty()) {
        m_source->unwatch(m_watchedName);
    }
    m_watchedName = name;
    if (m_source && !m_watchedName.isEmpty()) {
        m_source->watch(m_watchedName);
    }
}

void ScreenContentsItem::updateImage()
{
    const bool had = hasImage();
    m_image = (m_source && m_source->isShown()) ? m_source->image(m_outputName) : QImage();
    if (had != hasImage()) {
        Q_EMIT hasImageChanged();
    }
    update();
}
