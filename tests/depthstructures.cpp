/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../kcm/config_handler.h"
#include "../kcm/output_model.h"

#include <KLocalizedString>
#include <KScreen/Screen>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

using Layout = KScreen::Mode::Stereo3D;

// The HDMI 1.4a 3D structures beyond the first four, listed with the display's modes, and the
// per-output side by side (full) timing and stereo depth settings
class DepthStructuresTest : public QObject
{
    Q_OBJECT

    static KScreen::ModePtr mode(const QString &id, Layout layout, float rate = 60)
    {
        KScreen::ModePtr m(new KScreen::Mode);
        m->setId(id);
        m->setSize(QSize(1920, 1080));
        m->setRefreshRate(rate);
        m->setStereo3D(layout);
        return m;
    }

    static KScreen::OutputPtr configure(ConfigHandler &handler, const QList<KScreen::ModePtr> &modes, KScreen::Output::Capabilities capabilities)
    {
        KScreen::ConfigPtr config(new KScreen::Config);
        config->setScreen(KScreen::ScreenPtr(new KScreen::Screen));
        config->setSupportedFeatures(KScreen::Config::Feature::PerOutputScaling);
        KScreen::OutputPtr output(new KScreen::Output);
        output->setId(1);
        output->setName(QStringLiteral("HDMI-A-1"));
        output->setConnected(true);
        output->setEnabled(true);
        output->setCapabilities(capabilities);
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
        return output;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        qputenv("KSCREEN_BACKEND", "Fake");
        KLocalizedString::setApplicationDomain("kcm_kscreen");
        KLocalizedString::setLanguages({QStringLiteral("en_US")});
    }

    void everyStructureIsListed()
    {
        ConfigHandler handler;
        configure(handler,
                  {mode("2d", Layout::None),
                   mode("depth", Layout::LDepth),
                   mode("graphics", Layout::LDepthGraphics),
                   mode("line", Layout::LineAlternative),
                   mode("field", Layout::FieldAlternative, 50),
                   mode("fp", Layout::FramePacking)},
                  {});
        auto model = handler.outputModel();
        const QStringList rates = [&]() {
            QStringList ret;
            const auto list = model->data(model->index(0, 0), OutputModel::RefreshRatesRole).toList();
            for (const auto &entry : list) {
                ret << entry.toString();
            }
            return ret;
        }();
        // 2D first, then full resolution per eye, then field alternative and the picture and depth structures
        QCOMPARE(rates,
                 (QStringList{
                     QStringLiteral("60.00 Hz"),
                     QStringLiteral("60.00 Hz (3D frame packing, suggested for games)"),
                     QStringLiteral("60.00 Hz (3D line alternative, suggested for games)"),
                     QStringLiteral("50.00 Hz (3D field alternative)"),
                     QStringLiteral("60.00 Hz (3D picture and depth)"),
                     QStringLiteral("60.00 Hz (3D picture, depth and graphics)"),
                 }));
    }

    void settingsArePerOutput()
    {
        ConfigHandler handler;
        auto output = configure(handler,
                                {mode("2d", Layout::None), mode("full", Layout::SideBySideFull), mode("depth", Layout::LDepth)},
                                KScreen::Output::Capabilities(KScreen::Output::Capability::SideBySideFullTiming) | KScreen::Output::Capability::StereoDepth);
        auto model = handler.outputModel();
        const auto row = model->index(0, 0);
        QVERIFY(model->data(row, OutputModel::SideBySideFullModesRole).toBool());
        // the specification's timing and the estimated depth unless chosen otherwise
        QCOMPARE(model->data(row, OutputModel::SideBySideFullTimingRole).toUInt(), uint(KScreen::Output::SideBySideFullTiming::WholeLineDoubled));
        QCOMPARE(model->data(row, OutputModel::StereoDepthSourceRole).toUInt(), uint(KScreen::Output::StereoDepthSource::DesktopAndContent));
        QCOMPARE(model->data(row, OutputModel::StereoViewingDistanceRole).toUInt(), 0u);

        QSignalSpy dirty(&handler, &ConfigHandler::needsSaveChecked);
        QVERIFY(model->setData(row, uint(KScreen::Output::SideBySideFullTiming::BlankingKept), OutputModel::SideBySideFullTimingRole));
        QCOMPARE(output->sideBySideFullTiming(), KScreen::Output::SideBySideFullTiming::BlankingKept);
        QVERIFY(!dirty.isEmpty());
        QVERIFY(dirty.last().first().toBool());
        QVERIFY(model->setData(row, uint(KScreen::Output::SideBySideFullTiming::WholeLineDoubled), OutputModel::SideBySideFullTimingRole));
        QVERIFY(!dirty.last().first().toBool());

        QVERIFY(model->setData(row, uint(KScreen::Output::StereoDepthSource::Flat), OutputModel::StereoDepthSourceRole));
        QCOMPARE(output->stereoDepthSource(), KScreen::Output::StereoDepthSource::Flat);
        QVERIFY(dirty.last().first().toBool());
        QVERIFY(model->setData(row, uint(KScreen::Output::StereoDepthSource::DesktopAndContent), OutputModel::StereoDepthSourceRole));
        QVERIFY(!dirty.last().first().toBool());

        QVERIFY(model->setData(row, 250u, OutputModel::StereoViewingDistanceRole));
        QCOMPARE(output->stereoViewingDistance(), 250u);
        QVERIFY(dirty.last().first().toBool());
    }

    void noSideBySideFullModesNoTiming()
    {
        ConfigHandler handler;
        configure(handler, {mode("2d", Layout::None), mode("fp", Layout::FramePacking)}, KScreen::Output::Capability::SideBySideFullTiming);
        auto model = handler.outputModel();
        QVERIFY(!model->data(model->index(0, 0), OutputModel::SideBySideFullModesRole).toBool());
    }
};

QTEST_MAIN(DepthStructuresTest)
#include "depthstructures.moc"
