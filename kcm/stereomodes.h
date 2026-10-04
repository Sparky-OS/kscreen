/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <KLocalizedString>
#include <KScreen/Mode>

inline QString virtualStereoModeLabel(KScreen::Mode::Stereo3D layout, const QString &rateText)
{
    switch (layout) {
    case KScreen::Mode::Stereo3D::AnaglyphModern:
        return i18n("%1 (3D anaglyph, modern screens)", rateText);
    case KScreen::Mode::Stereo3D::AnaglyphCrt:
        return i18n("%1 (3D anaglyph, CRT)", rateText);
    case KScreen::Mode::Stereo3D::RowsLeftFirst:
        return i18n("%1 (3D rows, left eye first)", rateText);
    case KScreen::Mode::Stereo3D::RowsRightFirst:
        return i18n("%1 (3D rows, right eye first)", rateText);
    case KScreen::Mode::Stereo3D::ColumnsLeftFirst:
        return i18n("%1 (3D columns, left eye first)", rateText);
    case KScreen::Mode::Stereo3D::ColumnsRightFirst:
        return i18n("%1 (3D columns, right eye first)", rateText);
    case KScreen::Mode::Stereo3D::CheckerboardLeftFirst:
        return i18n("%1 (3D checkerboard, left eye first)", rateText);
    case KScreen::Mode::Stereo3D::CheckerboardRightFirst:
        return i18n("%1 (3D checkerboard, right eye first)", rateText);
    case KScreen::Mode::Stereo3D::SideBySideHalf:
        return i18n("%1 (3D side by side, turn the display's 3D on by hand)", rateText);
    case KScreen::Mode::Stereo3D::TopAndBottom:
        return i18n("%1 (3D top and bottom, turn the display's 3D on by hand)", rateText);
    case KScreen::Mode::Stereo3D::SequentialLeftFirst:
        return i18n("%1 (frame sequential, left eye first)", rateText);
    case KScreen::Mode::Stereo3D::SequentialRightFirst:
        return i18n("%1 (frame sequential, right eye first)", rateText);
    default:
        return rateText;
    }
}
