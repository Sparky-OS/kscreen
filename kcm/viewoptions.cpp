/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "viewoptions.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

using namespace Qt::StringLiterals;

static const QString s_configFile = u"kcm_kscreenrc"_s;
static const QString s_configGroup = u"ArrangementView"_s;
static const QString s_namesKey = u"ShowScreenNames"_s;
static const QString s_wheelKey = u"WheelPans"_s;

static const QString s_locatorService = u"org.kde.KWin"_s;
static const QString s_locatorPath = u"/org/kde/KWin/Effect/OutputLocator1"_s;
static const QString s_locatorInterface = u"org.kde.KWin.Effect.OutputLocator1"_s;

ViewOptions::ViewOptions(QObject *parent)
    : QObject(parent)
{
    const KConfigGroup group = KSharedConfig::openConfig(s_configFile)->group(s_configGroup);
    m_showScreenNames = group.readEntry(s_namesKey, false);
    m_wheelPans = group.readEntry(s_wheelKey, false);

    m_timer.setInterval(refreshInterval);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        sendToKWin(u"show"_s);
    });
}

ViewOptions::~ViewOptions()
{
    if (m_shown) {
        sendToKWin(u"hide"_s);
    }
}

bool ViewOptions::showScreenNames() const
{
    return m_showScreenNames;
}

void ViewOptions::setShowScreenNames(bool show)
{
    if (m_showScreenNames == show) {
        return;
    }
    m_showScreenNames = show;
    KConfigGroup group = KSharedConfig::openConfig(s_configFile)->group(s_configGroup);
    group.writeEntry(s_namesKey, show);
    group.sync();
    Q_EMIT showScreenNamesChanged();
    updateNames();
}

bool ViewOptions::wheelPans() const
{
    return m_wheelPans;
}

void ViewOptions::setWheelPans(bool pans)
{
    if (m_wheelPans == pans) {
        return;
    }
    m_wheelPans = pans;
    KConfigGroup group = KSharedConfig::openConfig(s_configFile)->group(s_configGroup);
    group.writeEntry(s_wheelKey, pans);
    group.sync();
    Q_EMIT wheelPansChanged();
}

bool ViewOptions::isActive() const
{
    return m_active;
}

void ViewOptions::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    m_active = active;
    Q_EMIT activeChanged();
    updateNames();
}

QString ViewOptions::errorString() const
{
    return m_errorString;
}

void ViewOptions::setErrorString(const QString &errorString)
{
    if (m_errorString == errorString) {
        return;
    }
    m_errorString = errorString;
    Q_EMIT errorStringChanged();
}

void ViewOptions::updateNames()
{
    const bool wanted = m_showScreenNames && m_active;
    if (wanted && !m_timer.isActive()) {
        // Ask KWin again, in case it could not before
        setErrorString(QString());
        m_timer.start();
        m_shown = true;
        sendToKWin(u"show"_s);
    } else if (!wanted && m_timer.isActive()) {
        m_timer.stop();
        m_shown = false;
        setErrorString(QString());
        sendToKWin(u"hide"_s);
    }
}

void ViewOptions::sendToKWin(const QString &method)
{
    const QDBusMessage message = QDBusMessage::createMethodCall(s_locatorService, s_locatorPath, s_locatorInterface, method);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, method](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        const QDBusPendingReply<> reply = *call;
        // Only what asks for the names is an error the person should hear of; hiding what is not there is not
        if (reply.isError() && method == u"show"_s && m_timer.isActive()) {
            setErrorString(reply.error().message());
        } else if (!reply.isError() && method == u"show"_s) {
            setErrorString(QString());
        }
    });
}
