/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../kcm/config_handler.h"
#include "../kcm/output_model.h"

#include <KLocalizedString>
#include <KScreen/Screen>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

using Layout = KScreen::Mode::Stereo3D;

// The per-output eye swap, Stereo KWin's one definitive swap: offered on an output with a stereo
// mode, never on a 2D one. The roles are looked up by their QML names, so the test builds against
// a model without them and fails there.
class SwapEyesTest : public QObject
{
    Q_OBJECT

    static KScreen::ModePtr mode(const QString &id, Layout layout, bool virtualStereo = false)
    {
        KScreen::ModePtr m(new KScreen::Mode);
        m->setId(id);
        m->setSize(QSize(1920, 1080));
        m->setRefreshRate(60);
        m->setStereo3D(layout);
        m->setVirtualStereo(virtualStereo);
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

    static int role(QAbstractItemModel *model, const char *name)
    {
        return model->roleNames().key(QByteArray(name), -1);
    }

    static bool stereoModes(ConfigHandler &handler)
    {
        auto model = handler.outputModel();
        return model->data(model->index(0, 0), role(model, "stereoModes")).toBool();
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        qputenv("KSCREEN_BACKEND", "Fake");
        KLocalizedString::setApplicationDomain("kcm_kscreen");
        KLocalizedString::setLanguages({QStringLiteral("en_US")});
    }

    void theSwitchHasItsRoles()
    {
        ConfigHandler handler;
        configure(handler, {mode("2d", Layout::None)}, {});
        auto model = handler.outputModel();
        QVERIFY(role(model, "stereoSwapEyes") >= 0);
        QVERIFY(role(model, "stereoModes") >= 0);
    }

    void offeredWithAStereoModeOnly()
    {
        const auto capabilities = KScreen::Output::Capabilities(KScreen::Output::Capability::VirtualStereo) | KScreen::Output::Capability::StereoSwapEyes;
        {
            // a 2D display with Anaglyph and Other stereo formats off lists 2D modes only: no switch
            ConfigHandler handler;
            configure(handler, {mode("2d", Layout::None), mode("2d-b", Layout::None)}, capabilities);
            QCOMPARE(stereoModes(handler), false);
        }
        {
            // a 3D television lists its HDMI 3D modes
            ConfigHandler handler;
            configure(handler, {mode("2d", Layout::None), mode("fp", Layout::FramePacking)}, capabilities);
            QCOMPARE(stereoModes(handler), true);
        }
        {
            // a display switched to 3D by hand, or in anaglyph: the virtual twins
            ConfigHandler handler;
            configure(handler, {mode("2d", Layout::None), mode("sbs", Layout::SideBySideHalf, true)}, capabilities);
            QCOMPARE(stereoModes(handler), true);
        }
        {
            // USB-C 3D glasses: the side by side (full) twin of their mode two eyes wide
            ConfigHandler handler;
            configure(handler, {mode("wide", Layout::None), mode("twin", Layout::SideBySideFull, true)}, capabilities);
            QCOMPARE(stereoModes(handler), true);
        }
    }

    void theSwitchSetsTheOutput()
    {
        ConfigHandler handler;
        auto output = configure(handler, {mode("2d", Layout::None), mode("fp", Layout::FramePacking)}, KScreen::Output::Capability::StereoSwapEyes);
        auto model = handler.outputModel();
        const auto row = model->index(0, 0);
        const int swap = role(model, "stereoSwapEyes");
        QVERIFY(swap >= 0);
        QCOMPARE(model->data(row, swap).toBool(), false);
        QSignalSpy dirty(&handler, &ConfigHandler::needsSaveChecked);
        QVERIFY(model->setData(row, true, swap));
        QCOMPARE(model->data(row, swap).toBool(), true);
        QCOMPARE(output->property("stereoSwapEyes").toBool(), true);
        QVERIFY(!dirty.isEmpty());
        QVERIFY(dirty.last().first().toBool());
        QVERIFY(model->setData(row, false, swap));
        QCOMPARE(output->property("stereoSwapEyes").toBool(), false);
        QVERIFY(!dirty.last().first().toBool());
    }
};

QTEST_MAIN(SwapEyesTest)

#include "swapeyes.moc"
