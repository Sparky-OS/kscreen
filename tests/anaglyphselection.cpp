/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../kcm/config_handler.h"
#include "../kcm/output_model.h"

#include <KLocalizedString>
#include <KScreen/Screen>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

using Layout = KScreen::Mode::Stereo3D;

class AnaglyphSelectionTest : public QObject
{
    Q_OBJECT

    static KScreen::ModePtr mode(const QString &id, Layout layout, float rate = 60,
                                 QSize size = {1360, 768}, bool virtualStereo = true)
    {
        KScreen::ModePtr m(new KScreen::Mode);
        m->setId(id);
        m->setSize(size);
        m->setRefreshRate(rate);
        m->setStereo3D(layout);
        m->setVirtualStereo(layout != Layout::None && virtualStereo);
        return m;
    }

    static KScreen::OutputPtr configure(ConfigHandler &handler, const QList<KScreen::ModePtr> &modes, bool secondOutput = false)
    {
        KScreen::ConfigPtr config(new KScreen::Config);
        config->setScreen(KScreen::ScreenPtr(new KScreen::Screen));
        KScreen::OutputPtr output(new KScreen::Output);
        output->setId(1);
        output->setName(QStringLiteral("AOC-test"));
        output->setConnected(true);
        output->setEnabled(true);
        output->setCapabilities(KScreen::Output::Capability::VirtualStereo);
        KScreen::ModeList list;
        for (const auto &m : modes) {
            list.insert(m->id(), m);
        }
        output->setModes(list);
        output->setCurrentModeId(modes.first()->id());
        output->setSize(modes.first()->size());
        output->setExplicitLogicalSize(modes.first()->size());
        config->addOutput(output);
        if (secondOutput) {
            auto second = output->clone();
            second->setId(2);
            second->setName(QStringLiteral("Second-test"));
            second->setPos(QPoint(modes.first()->size().width(), 0));
            config->addOutput(second);
        }
        handler.setConfig(config);
        return output;
    }

    static QVariantList entries(OutputModel *model, int role)
    {
        return model->data(model->index(0, 0), role).toList();
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        qputenv("KSCREEN_BACKEND", "Fake");
        KLocalizedString::setApplicationDomain("kcm_kscreen");
        KLocalizedString::setLanguages({QStringLiteral("en_US")});
    }

    void togglesArePerOutput()
    {
        ConfigHandler handler;
        auto first = configure(handler, {mode("base", Layout::None)}, true);
        auto second = handler.config()->output(2);
        QVERIFY(second);
        auto model = handler.outputModel();
        const auto firstIndex = model->indexForOutput(first);
        const auto secondIndex = model->indexForOutput(second);
        QSignalSpy dirty(&handler, &ConfigHandler::needsSaveChecked);

        QVERIFY(!first->anaglyph());
        QVERIFY(!second->anaglyph());
        QVERIFY(model->setData(firstIndex, true, OutputModel::AnaglyphRole));
        QVERIFY(first->anaglyph());
        QVERIFY(!second->anaglyph());
        QVERIFY(!first->otherStereoFormats());
        QCOMPARE(first->currentModeId(), QStringLiteral("base"));
        QVERIFY(!dirty.isEmpty());
        QVERIFY(dirty.last().first().toBool());

        QVERIFY(model->setData(firstIndex, false, OutputModel::AnaglyphRole));
        QVERIFY(!dirty.last().first().toBool());
        QVERIFY(model->setData(secondIndex, true, OutputModel::AnaglyphRole));
        QVERIFY(!first->anaglyph());
        QVERIFY(second->anaglyph());
        QVERIFY(dirty.last().first().toBool());
        QCOMPARE(second->currentModeId(), QStringLiteral("base"));
        QVERIFY(!model->data(firstIndex, OutputModel::AnaglyphRole).toBool());
        QVERIFY(model->data(secondIndex, OutputModel::AnaglyphRole).toBool());
    }

    void oneRateOffersAnaglyphInResolution_data()
    {
        QTest::addColumn<float>("rate");
        QTest::newRow("60 Hz") << 60.0f;
        QTest::newRow("AOC 1360x768") << 60.015f;
    }

    void oneRateOffersAnaglyphInResolution()
    {
        QFETCH(float, rate);
        ConfigHandler handler;
        auto output = configure(handler, {mode("base", Layout::None, rate), mode("modern", Layout::AnaglyphModern, rate), mode("crt", Layout::AnaglyphCrt, rate)});
        auto model = handler.outputModel();
        const auto row = model->index(0, 0);
        const auto resolutions = entries(model, OutputModel::ResolutionsRole);
        QCOMPARE(resolutions.size(), 3);
        QVERIFY(!resolutions[0].toString().contains("anaglyph"));
        QVERIFY(resolutions[1].toString().contains("anaglyph, modern screens"));
        QVERIFY(resolutions[2].toString().contains("anaglyph, CRT"));
        QCOMPARE(entries(model, OutputModel::RefreshRatesRole).size(), 1);

        QSignalSpy dirty(&handler, &ConfigHandler::needsSaveChecked);
        QVERIFY(model->setData(row, 1, OutputModel::ResolutionIndexRole));
        QCOMPARE(output->currentModeId(), QStringLiteral("modern"));
        QCOMPARE(output->currentMode()->refreshRate(), rate);
        QCOMPARE(model->data(row, OutputModel::ResolutionIndexRole).toInt(), 1);
        QVERIFY(!dirty.isEmpty());
        QVERIFY(dirty.last().first().toBool());
        QCOMPARE(entries(model, OutputModel::RefreshRatesRole).size(), 1);
        QVERIFY(!entries(model, OutputModel::RefreshRatesRole).first().toString().contains("anaglyph"));

        QVERIFY(model->setData(row, 2, OutputModel::ResolutionIndexRole));
        QCOMPARE(output->currentModeId(), QStringLiteral("crt"));
        QVERIFY(model->setData(row, 0, OutputModel::ResolutionIndexRole));
        QCOMPARE(output->currentModeId(), QStringLiteral("base"));
        QVERIFY(!dirty.last().first().toBool());
    }

    void refreshChangeKeepsAnaglyphVariant()
    {
        ConfigHandler handler;
        auto output = configure(handler, {mode("base60", Layout::None), mode("base75", Layout::None, 75),
                                         mode("modern60", Layout::AnaglyphModern), mode("modern75", Layout::AnaglyphModern, 75),
                                         mode("crt60", Layout::AnaglyphCrt), mode("crt75", Layout::AnaglyphCrt, 75)});
        auto model = handler.outputModel();
        const auto row = model->index(0, 0);
        QCOMPARE(entries(model, OutputModel::ResolutionsRole).size(), 3);
        QCOMPARE(entries(model, OutputModel::RefreshRatesRole).size(), 2);
        QVERIFY(model->setData(row, 1, OutputModel::ResolutionIndexRole));
        QCOMPARE(output->currentModeId(), QStringLiteral("modern60"));
        QVERIFY(model->setData(row, 0, OutputModel::RefreshRateIndexRole));
        QCOMPARE(output->currentModeId(), QStringLiteral("modern75"));
        QCOMPARE(model->data(row, OutputModel::ResolutionIndexRole).toInt(), 1);
        QVERIFY(model->setData(row, 2, OutputModel::ResolutionIndexRole));
        QCOMPARE(output->currentModeId(), QStringLiteral("crt75"));
        QVERIFY(model->setData(row, 0, OutputModel::ResolutionIndexRole));
        QCOMPARE(output->currentModeId(), QStringLiteral("base75"));
    }

    void changedSizeKeepsChosenAnaglyph()
    {
        ConfigHandler handler;
        auto output = configure(handler, {mode("base60", Layout::None), mode("modern60", Layout::AnaglyphModern),
                                         mode("largebase", Layout::None, 75, {1920, 1080}),
                                         mode("largemodern", Layout::AnaglyphModern, 75, {1920, 1080})});
        auto model = handler.outputModel();
        const auto row = model->index(0, 0);
        QVERIFY(model->setData(row, 1, OutputModel::ResolutionIndexRole));
        QCOMPARE(output->currentModeId(), QStringLiteral("largemodern"));
        QCOMPARE(output->size(), QSize(1920, 1080));
        QCOMPARE(output->currentMode()->refreshRate(), 75);
    }

    void displaySpecificStereoStaysInRefresh()
    {
        ConfigHandler handler;
        auto output = configure(handler, {mode("base", Layout::None), mode("hdmi", Layout::FramePacking, 60, {1360, 768}, false),
                                         mode("modern", Layout::AnaglyphModern), mode("crt", Layout::AnaglyphCrt)});
        auto model = handler.outputModel();
        const auto row = model->index(0, 0);
        QCOMPARE(entries(model, OutputModel::ResolutionsRole).size(), 3);
        const auto rates = entries(model, OutputModel::RefreshRatesRole);
        QCOMPARE(rates.size(), 2);
        QVERIFY(rates[1].toString().contains("frame packing"));
        QVERIFY(model->setData(row, 1, OutputModel::RefreshRateIndexRole));
        QCOMPARE(output->currentModeId(), QStringLiteral("hdmi"));
        QCOMPARE(model->data(row, OutputModel::ResolutionIndexRole).toInt(), 0);
    }

    void modesArrivingRefreshBothSelectors()
    {
        ConfigHandler handler;
        auto output = configure(handler, {mode("base", Layout::None)});
        auto model = handler.outputModel();
        QCOMPARE(entries(model, OutputModel::ResolutionsRole).size(), 1);
        QSignalSpy changed(model, &OutputModel::dataChanged);
        auto modes = output->modes();
        modes.insert("modern", mode("modern", Layout::AnaglyphModern));
        modes.insert("crt", mode("crt", Layout::AnaglyphCrt));
        output->setModes(modes);
        QVERIFY(!changed.isEmpty());
        const auto roles = changed.last()[2].value<QList<int>>();
        QVERIFY(roles.contains(OutputModel::ResolutionsRole));
        QVERIFY(roles.contains(OutputModel::RefreshRatesRole));
        QVERIFY(roles.contains(OutputModel::RefreshRateIndexRole));
        QCOMPARE(entries(model, OutputModel::ResolutionsRole).size(), 3);
    }
};

QTEST_GUILESS_MAIN(AnaglyphSelectionTest)
#include "anaglyphselection.moc"
