/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../kcm/config_handler.h"
#include "../kcm/output_model.h"

#include <KLocalizedString>
#include <KScreen/Screen>
#include <QStandardPaths>
#include <QTest>

using Layout = KScreen::Mode::Stereo3D;

// DisplayPort's frame sequential stereo: a display 3D mode when the display declares it, a twin of a
// 2D mode under "Other stereo formats" otherwise, each with its own name beside KWin's software twins
class SequentialDisplayPortTest : public QObject
{
    Q_OBJECT

    static KScreen::ModePtr mode(const QString &id, Layout layout, float rate, bool virtualStereo = false)
    {
        KScreen::ModePtr m(new KScreen::Mode);
        m->setId(id);
        m->setSize(QSize(1920, 1080));
        m->setRefreshRate(rate);
        m->setStereo3D(layout);
        m->setVirtualStereo(virtualStereo);
        return m;
    }

    static QStringList rates(const QList<KScreen::ModePtr> &modes)
    {
        ConfigHandler handler;
        KScreen::ConfigPtr config(new KScreen::Config);
        config->setScreen(KScreen::ScreenPtr(new KScreen::Screen));
        config->setSupportedFeatures(KScreen::Config::Feature::PerOutputScaling);
        KScreen::OutputPtr output(new KScreen::Output);
        output->setId(1);
        output->setName(QStringLiteral("DP-1"));
        output->setConnected(true);
        output->setEnabled(true);
        KScreen::ModeList list;
        for (const auto &m : modes) {
            list.insert(m->id(), m);
        }
        output->setModes(list);
        output->setCurrentModeId(modes.first()->id());
        output->setSize(modes.first()->size());
        output->setExplicitLogicalSize(modes.first()->size());
        config->addOutput(output);
        handler.setConfig(config);
        auto model = handler.outputModel();
        QStringList ret;
        const auto entries = model->data(model->index(0, 0), OutputModel::RefreshRatesRole).toList();
        for (const auto &entry : entries) {
            ret << entry.toString();
        }
        return ret;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        qputenv("KSCREEN_BACKEND", "Fake");
        KLocalizedString::setApplicationDomain("kcm_kscreen");
        KLocalizedString::setLanguages({QStringLiteral("en_US")});
    }

    void declaredAndTwins()
    {
        // the display declares 60 Hz; the 120 Hz twins: the DisplayPort one and KWin's two software ones
        QCOMPARE(rates({mode("2d120", Layout::None, 120),
                        mode("2d60", Layout::None, 60),
                        mode("declared", Layout::SequentialDisplayPort, 60),
                        mode("fp", Layout::FramePacking, 24),
                        mode("dp-twin", Layout::SequentialDisplayPort, 120, true),
                        mode("sw-left", Layout::SequentialLeftFirst, 120, true),
                        mode("sw-right", Layout::SequentialRightFirst, 120, true)}),
                 (QStringList{
                     QStringLiteral("120.00 Hz"),
                     QStringLiteral("60.00 Hz"),
                     QStringLiteral("24.00 Hz (3D frame packing, suggested for movies)"),
                     QStringLiteral("60.00 Hz (3D frame sequential, DisplayPort)"),
                     // among the twins the signalled one comes first; KWin's own two stay beside it
                     QStringLiteral("120.00 Hz (frame sequential, DisplayPort)"),
                     QStringLiteral("120.00 Hz (frame sequential, left eye first)"),
                     QStringLiteral("120.00 Hz (frame sequential, right eye first)"),
                 }));
    }
};

QTEST_MAIN(SequentialDisplayPortTest)
#include "sequentialdisplayport.moc"
