/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../kcm/stereomodes.h"
#include <QTest>

class StereoModesTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void labels()
    {
        KLocalizedString::setLanguages({QStringLiteral("en_US")});
        using Layout = KScreen::Mode::Stereo3D;
        const QList<std::pair<Layout, QString>> expected{
            {Layout::AnaglyphModern, QStringLiteral("60 Hz (3D anaglyph, modern screens)")},
            {Layout::AnaglyphCrt, QStringLiteral("60 Hz (3D anaglyph, CRT)")},
            {Layout::SideBySideHalf, QStringLiteral("60 Hz (3D side by side, turn the display's 3D on by hand)")},
            {Layout::TopAndBottom, QStringLiteral("60 Hz (3D top and bottom, turn the display's 3D on by hand)")},
            {Layout::RowsLeftFirst, QStringLiteral("60 Hz (3D rows, left eye first)")},
            {Layout::RowsRightFirst, QStringLiteral("60 Hz (3D rows, right eye first)")},
            {Layout::ColumnsLeftFirst, QStringLiteral("60 Hz (3D columns, left eye first)")},
            {Layout::ColumnsRightFirst, QStringLiteral("60 Hz (3D columns, right eye first)")},
            {Layout::CheckerboardLeftFirst, QStringLiteral("60 Hz (3D checkerboard, left eye first)")},
            {Layout::CheckerboardRightFirst, QStringLiteral("60 Hz (3D checkerboard, right eye first)")},
            {Layout::SequentialLeftFirst, QStringLiteral("60 Hz (frame sequential, left eye first)")},
            {Layout::SequentialRightFirst, QStringLiteral("60 Hz (frame sequential, right eye first)")},
            {Layout::SideBySideFull, QStringLiteral("60 Hz (3D side by side full, for USB-C 3D glasses)")},
        };
        for (const auto &[layout, label] : expected) {
            QCOMPARE(virtualStereoModeLabel(layout, QStringLiteral("60 Hz")), label);
        }
    }

#if HAVE_KSCREEN_STEREO_DEPTH
    void depthStructureLabels()
    {
        KLocalizedString::setLanguages({QStringLiteral("en_US")});
        using Layout = KScreen::Mode::Stereo3D;
        QCOMPARE(depthStructureLabel(Layout::FieldAlternative, QStringLiteral("50 Hz"), false), QStringLiteral("50 Hz (3D field alternative)"));
        QCOMPARE(depthStructureLabel(Layout::LineAlternative, QStringLiteral("60 Hz"), true),
                 QStringLiteral("60 Hz (3D line alternative, suggested for games)"));
        QCOMPARE(depthStructureLabel(Layout::LineAlternative, QStringLiteral("24 Hz"), false), QStringLiteral("24 Hz (3D line alternative)"));
        QCOMPARE(depthStructureLabel(Layout::LDepth, QStringLiteral("60 Hz"), true), QStringLiteral("60 Hz (3D picture and depth)"));
        QCOMPARE(depthStructureLabel(Layout::LDepthGraphics, QStringLiteral("50 Hz"), false), QStringLiteral("50 Hz (3D picture, depth and graphics)"));
        QVERIFY(isPictureAndDepth(Layout::LDepth));
        QVERIFY(isPictureAndDepth(Layout::LDepthGraphics));
        QVERIFY(!isPictureAndDepth(Layout::FramePacking));
    }
#endif
};
QTEST_GUILESS_MAIN(StereoModesTest)
#include "stereomodes.moc"
