/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../kcm/config_handler.h"
#include "../kcm/output_model.h"

#include <KLocalizedString>
#include <KScreen/Screen>
#include <QMetaObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

// The output model behind the extra menus of the Display Settings page: flipped transforms, X and Y
// position, the physical size and custom modes. The roles and methods are looked up by their QML names,
// so the test builds against a model without them and fails there.
class OutputMenusTest : public QObject
{
    Q_OBJECT

    struct Setup {
        QSize modeSize = QSize(1920, 1080);
        KScreen::Output::Rotation rotation = KScreen::Output::None;
        QSize sizeMm = QSize(-1, -1);
        KScreen::Output::Capabilities capabilities;
        int outputs = 1;
    };

    static KScreen::OutputPtr configure(ConfigHandler &handler, const Setup &setup)
    {
        KScreen::ConfigPtr config(new KScreen::Config);
        config->setScreen(KScreen::ScreenPtr(new KScreen::Screen));
        config->setSupportedFeatures(KScreen::Config::Feature::PerOutputScaling);
        KScreen::OutputPtr first;
        for (int i = 0; i < setup.outputs; ++i) {
            KScreen::ModePtr mode(new KScreen::Mode);
            mode->setId(QStringLiteral("m%1").arg(i));
            mode->setSize(setup.modeSize);
            mode->setRefreshRate(60);
            KScreen::OutputPtr output(new KScreen::Output);
            output->setId(i + 1);
            output->setName(QStringLiteral("HDMI-A-%1").arg(i + 1));
            output->setConnected(true);
            output->setEnabled(true);
            output->setCapabilities(setup.capabilities);
            output->setSizeMm(setup.sizeMm);
            KScreen::ModeList list;
            list.insert(mode->id(), mode);
            output->setModes(list);
            output->setCurrentModeId(mode->id());
            output->setRotation(setup.rotation);
            output->setPos(QPoint(i * setup.modeSize.width(), 0));
            output->setSize(setup.modeSize);
            output->setExplicitLogicalSize(setup.modeSize);
            config->addOutput(output);
            if (!first) {
                first = output;
            }
        }
        handler.setConfig(config);
        return first;
    }

    static int role(QAbstractItemModel *model, const char *name)
    {
        return model->roleNames().key(QByteArray(name), -1);
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        qputenv("KSCREEN_BACKEND", "Fake");
        KLocalizedString::setApplicationDomain("kcm_kscreen");
        KLocalizedString::setLanguages({QStringLiteral("en_US")});
    }

    void flippingKeepsTheRotation()
    {
        ConfigHandler handler;
        auto output = configure(handler, {});
        auto model = handler.outputModel();
        const auto row = model->index(0, 0);
        const int flipped = role(model, "flipped");
        QVERIFY2(flipped >= 0, "no flipped role");
        const int rotation = role(model, "rotation");
        QCOMPARE(model->data(row, flipped).toBool(), false);

        QSignalSpy dirty(&handler, &ConfigHandler::needsSaveChecked);
        QVERIFY(model->setData(row, true, flipped));
        QCOMPARE(output->rotation(), KScreen::Output::Flipped);
        QCOMPARE(model->data(row, flipped).toBool(), true);
        // the rotation buttons keep meaning the turn alone
        QCOMPARE(model->data(row, rotation).toInt(), int(KScreen::Output::None));
        QVERIFY(!dirty.isEmpty());
        QVERIFY(dirty.last().first().toBool());

        QVERIFY(model->setData(row, QVariant::fromValue(KScreen::Output::Left), rotation));
        QCOMPARE(output->rotation(), KScreen::Output::Flipped90);
        QCOMPARE(model->data(row, flipped).toBool(), true);
        QCOMPARE(model->data(row, rotation).toInt(), int(KScreen::Output::Left));

        QVERIFY(model->setData(row, QVariant::fromValue(KScreen::Output::Inverted), rotation));
        QCOMPARE(output->rotation(), KScreen::Output::Flipped180);
        QVERIFY(model->setData(row, QVariant::fromValue(KScreen::Output::Right), rotation));
        QCOMPARE(output->rotation(), KScreen::Output::Flipped270);

        QVERIFY(model->setData(row, false, flipped));
        QCOMPARE(output->rotation(), KScreen::Output::Right);
        QCOMPARE(model->data(row, flipped).toBool(), false);
        QVERIFY(model->setData(row, QVariant::fromValue(KScreen::Output::None), rotation));
        QVERIFY(!dirty.last().first().toBool());
    }

    void anOutputKWinReportsFlippedShowsItsFlip()
    {
        ConfigHandler handler;
        Setup setup;
        setup.rotation = KScreen::Output::Flipped270;
        configure(handler, setup);
        auto model = handler.outputModel();
        const auto row = model->index(0, 0);
        QVERIFY(role(model, "flipped") >= 0);
        QCOMPARE(model->data(row, role(model, "flipped")).toBool(), true);
        QCOMPARE(model->data(row, role(model, "rotation")).toInt(), int(KScreen::Output::Right));
    }

    void aQuarterTurnAndAFlipSwapTheSize()
    {
        ConfigHandler handler;
        auto output = configure(handler, {});
        auto model = handler.outputModel();
        const auto row = model->index(0, 0);
        const int flipped = role(model, "flipped");
        QVERIFY(flipped >= 0);
        QCOMPARE(model->data(row, role(model, "size")).toSize(), QSize(1920, 1080));
        QVERIFY(model->setData(row, true, flipped));
        QCOMPARE(model->data(row, role(model, "size")).toSize(), QSize(1920, 1080));
        QVERIFY(model->setData(row, QVariant::fromValue(KScreen::Output::Left), role(model, "rotation")));
        QCOMPARE(output->rotation(), KScreen::Output::Flipped90);
        QCOMPARE(model->data(row, role(model, "size")).toSize(), QSize(1080, 1920));
    }
};

QTEST_MAIN(OutputMenusTest)

#include "outputmenus.moc"
