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

    static bool addCustomMode(QAbstractItemModel *model, int row, int width, int height, double refresh, bool reduced)
    {
        bool done = false;
        const bool found = QMetaObject::invokeMethod(model,
                                                     "addCustomMode",
                                                     Q_RETURN_ARG(bool, done),
                                                     Q_ARG(int, row),
                                                     Q_ARG(int, width),
                                                     Q_ARG(int, height),
                                                     Q_ARG(double, refresh),
                                                     Q_ARG(bool, reduced));
        if (!found) {
            QTest::qFail("no addCustomMode method", __FILE__, __LINE__);
        }
        return done;
    }

    static bool removeCustomMode(QAbstractItemModel *model, int row, int index)
    {
        bool done = false;
        const bool found = QMetaObject::invokeMethod(model, "removeCustomMode", Q_RETURN_ARG(bool, done), Q_ARG(int, row), Q_ARG(int, index));
        if (!found) {
            QTest::qFail("no removeCustomMode method", __FILE__, __LINE__);
        }
        return done;
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
    void aScreenGoesWhereTheNumbersSay()
    {
        ConfigHandler handler;
        Setup setup;
        setup.outputs = 2;
        auto first = configure(handler, setup);
        auto model = handler.outputModel();
        const int normalized = role(model, "normalizedPosition");
        QVERIFY(normalized >= 0);
        const auto positionOf = [&](int row) {
            return model->data(model->index(row, 0), normalized).toPoint();
        };
        QCOMPARE(positionOf(0), QPoint(0, 0));
        QCOMPARE(positionOf(1), QPoint(1920, 0));

        QSignalSpy dirty(&handler, &ConfigHandler::needsSaveChecked);
        bool done = false;
        QVERIFY2(QMetaObject::invokeMethod(model, "setNormalizedPosition", Q_RETURN_ARG(bool, done), Q_ARG(int, 1), Q_ARG(QPoint, QPoint(1920, 200))),
                 "no setNormalizedPosition method");
        QVERIFY(done);
        QCOMPARE(positionOf(1), QPoint(1920, 200));
        QCOMPARE(positionOf(0), QPoint(0, 0));
        QCOMPARE(handler.config()->outputs().value(2)->pos(), QPoint(1920, 200));
        QVERIFY(!dirty.isEmpty());
        QVERIFY(dirty.last().first().toBool());

        // the numbers are exact: nothing snaps the screen back to the edge of the other
        QVERIFY(QMetaObject::invokeMethod(model, "setNormalizedPosition", Q_RETURN_ARG(bool, done), Q_ARG(int, 1), Q_ARG(QPoint, QPoint(1925, 3))));
        QCOMPARE(positionOf(1), QPoint(1925, 3));
        QCOMPARE(first->pos(), QPoint(0, 0));

        // the place it already has is no change
        QVERIFY(QMetaObject::invokeMethod(model, "setNormalizedPosition", Q_RETURN_ARG(bool, done), Q_ARG(int, 1), Q_ARG(QPoint, QPoint(1925, 3))));
        QVERIFY(!done);
    }

    void positionsAreCountedFromTheTopLeftOfAllScreens()
    {
        ConfigHandler handler;
        Setup setup;
        setup.outputs = 2;
        configure(handler, setup);
        auto model = handler.outputModel();
        const int normalized = role(model, "normalizedPosition");
        bool done = false;
        // left of the other screen: the arrangement is counted from its new top left corner
        QVERIFY(QMetaObject::invokeMethod(model, "setNormalizedPosition", Q_RETURN_ARG(bool, done), Q_ARG(int, 1), Q_ARG(QPoint, QPoint(-100, -50))));
        QVERIFY(done);
        QCOMPARE(model->data(model->index(1, 0), normalized).toPoint(), QPoint(0, 0));
        QCOMPARE(model->data(model->index(0, 0), normalized).toPoint(), QPoint(100, 50));
    }
    void theModelHoldsThePhysicalSize()
    {
        {
            ConfigHandler handler;
            Setup setup;
            setup.sizeMm = QSize(527, 296);
            configure(handler, setup);
            auto model = handler.outputModel();
            const int size = role(model, "physicalSize");
            QVERIFY2(size >= 0, "no physicalSize role");
            QCOMPARE(model->data(model->index(0, 0), size).toSize(), QSize(527, 296));
        }
        {
            // KWin reports no size for a virtual output, and some displays do not say
            ConfigHandler handler;
            configure(handler, {});
            auto model = handler.outputModel();
            const int size = role(model, "physicalSize");
            QVERIFY(size >= 0);
            const QSize unknown = model->data(model->index(0, 0), size).toSize();
            QVERIFY2(unknown.width() <= 0 || unknown.height() <= 0, qPrintable(QStringLiteral("%1x%2").arg(unknown.width()).arg(unknown.height())));
        }
    }

    void theSizeStaysWhateverTheTurn()
    {
        ConfigHandler handler;
        Setup setup;
        setup.sizeMm = QSize(527, 296);
        configure(handler, setup);
        auto model = handler.outputModel();
        QVERIFY(model->setData(model->index(0, 0), QVariant::fromValue(KScreen::Output::Left), role(model, "rotation")));
        QCOMPARE(model->data(model->index(0, 0), role(model, "physicalSize")).toSize(), QSize(527, 296));
    }


    void customModesAreAddedAndRemoved()
    {
        ConfigHandler handler;
        Setup setup;
        setup.capabilities = KScreen::Output::Capability::CustomModes;
        auto output = configure(handler, setup);
        auto model = handler.outputModel();
        const int custom = role(model, "customModes");
        QVERIFY2(custom >= 0, "no customModes role");
        const auto row = model->index(0, 0);
        QCOMPARE(model->data(row, custom).toStringList().size(), 0);

        QSignalSpy dirty(&handler, &ConfigHandler::needsSaveChecked);
        QVERIFY(addCustomMode(model, 0, 1000, 700, 60.0, false));
        QCOMPARE(output->customModes().size(), 1);
        QCOMPARE(output->customModes().first().size, QSize(1000, 700));
        QCOMPARE(output->customModes().first().refreshRate, 60.0f);
        QVERIFY(output->customModes().first().flags & KScreen::ModeInfo::Flag::Custom);
        QVERIFY(!(output->customModes().first().flags & KScreen::ModeInfo::Flag::ReducedBlanking));
        QCOMPARE(model->data(row, custom).toStringList().size(), 1);
        QVERIFY2(model->data(row, custom).toStringList().first().contains(QStringLiteral("1000")), "the list names the mode");
        QVERIFY(!dirty.isEmpty());
        QVERIFY2(dirty.last().first().toBool(), "a new custom mode is something to apply");

        QVERIFY(addCustomMode(model, 0, 1920, 1080, 144.0, true));
        QCOMPARE(output->customModes().size(), 2);
        QVERIFY(output->customModes().last().flags & KScreen::ModeInfo::Flag::ReducedBlanking);

        // a custom mode that is already there is no new one
        QVERIFY(!addCustomMode(model, 0, 1000, 700, 60.0, false));
        QCOMPARE(output->customModes().size(), 2);

        QVERIFY(removeCustomMode(model, 0, 0));
        QCOMPARE(output->customModes().size(), 1);
        QCOMPARE(output->customModes().first().size, QSize(1920, 1080));
        QVERIFY(!removeCustomMode(model, 0, 5));
        QVERIFY(!removeCustomMode(model, 0, -1));
        QVERIFY(removeCustomMode(model, 0, 0));
        QVERIFY(!dirty.last().first().toBool());
    }

    void customModesNeedTheCapabilityAndSaneNumbers()
    {
        {
            // KWin without custom modes: nothing to add
            ConfigHandler handler;
            auto output = configure(handler, {});
            QVERIFY(!addCustomMode(handler.outputModel(), 0, 1000, 700, 60.0, false));
            QCOMPARE(output->customModes().size(), 0);
        }
        ConfigHandler handler;
        Setup setup;
        setup.capabilities = KScreen::Output::Capability::CustomModes;
        auto output = configure(handler, setup);
        auto model = handler.outputModel();
        QVERIFY(!addCustomMode(model, 0, 0, 700, 60.0, false));
        QVERIFY(!addCustomMode(model, 0, 1000, -1, 60.0, false));
        QVERIFY(!addCustomMode(model, 0, 100000, 700, 60.0, false));
        QVERIFY(!addCustomMode(model, 0, 1000, 700, 0.0, false));
        QVERIFY(!addCustomMode(model, 0, 1000, 700, 5000.0, false));
        QVERIFY(!addCustomMode(model, 3, 1000, 700, 60.0, false));
        QCOMPARE(output->customModes().size(), 0);
    }
};

QTEST_MAIN(OutputMenusTest)

#include "outputmenus.moc"
