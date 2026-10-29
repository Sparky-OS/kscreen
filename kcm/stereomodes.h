/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "config-stereo.h"

#if HAVE_KSCREEN_STEREO
#include <KLocalizedString>
#include <KScreen/Mode>

inline bool isAnaglyph(KScreen::Mode::Stereo3D layout)
{
    return layout == KScreen::Mode::Stereo3D::AnaglyphModern || layout == KScreen::Mode::Stereo3D::AnaglyphCrt;
}

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
    case KScreen::Mode::Stereo3D::SideBySideFull:
        return i18n("%1 (3D side by side full, for USB-C 3D glasses)", rateText);
#if HAVE_KSCREEN_STEREO_SEQUENTIAL_DISPLAYPORT
    // the twin of a 2D mode the display did not declare: the source still alternates and signals the eyes
    case KScreen::Mode::Stereo3D::SequentialDisplayPort:
        return i18n("%1 (frame sequential, DisplayPort)", rateText);
#endif
    default:
        return rateText;
    }
}
#endif

#if HAVE_KSCREEN_STEREO_SEQUENTIAL_DISPLAYPORT
/**
 * The label of DisplayPort's frame sequential stereo when the display declares it (an EDID detailed
 * timing or a DisplayID stereo block): the source sends one eye per frame and tells the display which.
 */
inline QString sequentialDisplayPortLabel(const QString &rateText)
{
    return i18nc("@item:inlistbox refresh rate of a DisplayPort 3D mode; %1 is the rate", "%1 (3D frame sequential, DisplayPort)", rateText);
}
#endif

#if HAVE_KSCREEN_STEREO_DEPTH
inline bool isPictureAndDepth(KScreen::Mode::Stereo3D layout)
{
    return layout == KScreen::Mode::Stereo3D::LDepth || layout == KScreen::Mode::Stereo3D::LDepthGraphics;
}

/**
 * The label of an HDMI 1.4a 3D structure beyond the four first listed (Appendix H): field and
 * line alternative, and the picture and depth structures, from which the display makes the
 * views. Line alternative sends each eye at full resolution, so it is suggested for games
 * at game rates, as side by side (full) and frame packing are.
 */
inline QString depthStructureLabel(KScreen::Mode::Stereo3D layout, const QString &rateText, bool gameRate)
{
    switch (layout) {
    case KScreen::Mode::Stereo3D::FieldAlternative:
        return i18nc("@item:inlistbox refresh rate of an HDMI 3D mode; %1 is the rate", "%1 (3D field alternative)", rateText);
    case KScreen::Mode::Stereo3D::LineAlternative:
        if (gameRate) {
            return i18nc("@item:inlistbox refresh rate of an HDMI 3D mode; %1 is the rate", "%1 (3D line alternative, suggested for games)", rateText);
        }
        return i18nc("@item:inlistbox refresh rate of an HDMI 3D mode; %1 is the rate", "%1 (3D line alternative)", rateText);
    case KScreen::Mode::Stereo3D::LDepth:
        return i18nc("@item:inlistbox refresh rate of an HDMI 3D mode; %1 is the rate", "%1 (3D picture and depth)", rateText);
    case KScreen::Mode::Stereo3D::LDepthGraphics:
        return i18nc("@item:inlistbox refresh rate of an HDMI 3D mode; %1 is the rate", "%1 (3D picture, depth and graphics)", rateText);
    default:
        return rateText;
    }
}
#endif
