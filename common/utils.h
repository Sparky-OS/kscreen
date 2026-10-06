/*
    SPDX-FileCopyrightText: 2013 Daniel Vrátil <dvratil@redhat.com>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QSize>
#include <QString>

#include <kscreen/mode.h>
#include <kscreen/output.h>
#include <kscreen/types.h>

#include "config-stereo3d.h"

namespace Utils
{
QString outputName(const KScreen::Output *output, bool shouldShowSerialNumber = false, bool shouldShowConnector = false);
QString outputName(const KScreen::OutputPtr &output, bool shouldShowSerialNumber = false, bool shouldShowConnector = false);

QString sizeToString(const QSize &size);
KScreen::ModePtr biggestMode(const KScreen::ModeList &modes);
// The HDMI 3D structure of a mode comes from libkscreen. A libkscreen without it (KDE's) has no 3D modes
// to show, so the structure of every mode is None and the 3D parts of the settings stay empty.
#if HAVE_LIBKSCREEN_STEREO3D
using Stereo3D = KScreen::Mode::Stereo3D;
inline Stereo3D stereo3D(const KScreen::ModePtr &mode)
{
    return mode->stereo3D();
}
#else
enum class Stereo3D {
    None,
    SideBySideHalf,
    TopAndBottom,
    FramePacking,
    SideBySideFull,
};
inline Stereo3D stereo3D(const KScreen::ModePtr &)
{
    return Stereo3D::None;
}
#endif

// an HDMI 3D mode: listed with the others, but only ever chosen by the user
bool isStereo3D(const KScreen::ModePtr &mode);
}
