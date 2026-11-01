/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QSet>
#include <QTimer>

#include <optional>

/*
 * What each screen shows, for the previews in the arrangement view.
 *
 * The screens are captured through KWin's org.kde.KWin.ScreenShot2 interface, one
 * capture per screen and second while the previews are shown and the view is on
 * screen. The image is read on a worker thread, so a capture never blocks the view.
 *
 * Whether the previews are shown is the person's choice. Until they make one, the
 * previews are shown on mains power and not on battery.
 */
class ScreenContents : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool shown READ isShown WRITE setShown NOTIFY shownChanged)
    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)

public:
    explicit ScreenContents(QObject *parent = nullptr);
    ~ScreenContents() override;

    bool isShown() const;
    void setShown(bool shown);

    // Whether the view that shows the previews is on screen
    bool isActive() const;
    void setActive(bool active);

    QString errorString() const;

    void watch(const QString &outputName);
    void unwatch(const QString &outputName);
    QImage image(const QString &outputName) const;

    static constexpr int captureInterval = 1000;
    static constexpr int maximumPreviewSize = 512;

Q_SIGNALS:
    void shownChanged();
    void activeChanged();
    void errorStringChanged();
    void imageChanged(const QString &outputName);

private:
    void updateCapturing();
    void captureAll();
    void capture(const QString &outputName);
    void setErrorString(const QString &errorString);
    void setOnBattery(bool onBattery);
    void readOnBattery();

private Q_SLOTS:
    void upowerPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    QTimer m_timer;
    QHash<QString, int> m_watched;
    QHash<QString, QImage> m_images;
    QSet<QString> m_pending;
    std::optional<bool> m_choice;
    bool m_onBattery = false;
    bool m_active = false;
    QString m_errorString;
};

class ScreenContentsItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(ScreenContents *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString outputName READ outputName WRITE setOutputName NOTIFY outputNameChanged)
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY hasImageChanged)

public:
    explicit ScreenContentsItem(QQuickItem *parent = nullptr);
    ~ScreenContentsItem() override;

    ScreenContents *source() const;
    void setSource(ScreenContents *source);

    QString outputName() const;
    void setOutputName(const QString &outputName);

    bool hasImage() const;

    void paint(QPainter *painter) override;

Q_SIGNALS:
    void sourceChanged();
    void outputNameChanged();
    void hasImageChanged();

protected:
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    void updateWatching();
    void updateImage();

    QPointer<ScreenContents> m_source;
    QString m_outputName;
    QString m_watchedName;
    QImage m_image;
};
