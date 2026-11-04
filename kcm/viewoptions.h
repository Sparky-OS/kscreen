/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QObject>
#include <QTimer>

/*
 * The choices of the arrangement view that the person keeps, in the group ArrangementView of kcm_kscreenrc.
 *
 * Show Screen Names keeps the labels of Identify Screens on every screen as long as the view is on screen.
 * KWin's OutputLocator effect shows them for two and a half seconds, and showing them again extends that,
 * so they are shown again every two seconds and hidden when the choice is switched off or the view goes.
 *
 * The plain wheel pans the view, as it does in wdisplays, instead of doing nothing over it.
 */
class ViewOptions : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool showScreenNames READ showScreenNames WRITE setShowScreenNames NOTIFY showScreenNamesChanged)
    Q_PROPERTY(bool wheelPans READ wheelPans WRITE setWheelPans NOTIFY wheelPansChanged)
    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)

public:
    explicit ViewOptions(QObject *parent = nullptr);
    ~ViewOptions() override;

    bool showScreenNames() const;
    void setShowScreenNames(bool show);

    bool wheelPans() const;
    void setWheelPans(bool pans);

    // Whether the view is on screen
    bool isActive() const;
    void setActive(bool active);

    // Why the names cannot be shown, when KWin says so; empty otherwise
    QString errorString() const;

    static constexpr int refreshInterval = 2000;

Q_SIGNALS:
    void showScreenNamesChanged();
    void wheelPansChanged();
    void activeChanged();
    void errorStringChanged();

private:
    void updateNames();
    void sendToKWin(const QString &method);
    void setErrorString(const QString &errorString);

    QTimer m_timer;
    bool m_showScreenNames = false;
    bool m_wheelPans = false;
    bool m_active = false;
    bool m_shown = false;
    QString m_errorString;
};
