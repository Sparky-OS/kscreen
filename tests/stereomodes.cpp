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
        };
        for (const auto &[layout, label] : expected) {
            QCOMPARE(virtualStereoModeLabel(layout, QStringLiteral("60 Hz")), label);
        }
    }
};
QTEST_GUILESS_MAIN(StereoModesTest)
#include "stereomodes.moc"
