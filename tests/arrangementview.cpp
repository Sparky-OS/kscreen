/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Drives the arrangement view of the installed or the freshly built KCM, inside a running
// compositor with two or more screens: snapping and Shift, zoom, pan, Fit All and the
// screen contents.
//
// It runs only when KSCREEN_TEST_ARRANGEMENT is set, because it needs a compositor.
// KSCREEN_TEST_KCM: the KCM plugin file to load; without it, the one found on the plugin path
// KSCREEN_TEST_CAPTURE_DIR: where to write pictures of the view, with what they should show
// KSCREEN_TEST_SCREEN_COLORS: "NAME=#rrggbb;..." the colour each screen shows
// KSCREEN_TEST_UPOWER_MOCK: set when a python-dbusmock UPower runs on the system bus
// KSCREEN_TEST_EXPECT_REFUSAL: set when the compositor refuses the KCM's captures
// KSCREEN_TEST_STEREO_SCREEN: a screen in a side by side 3D mode that shows red on its left half and blue on its right

#include <KPluginMetaData>
#include <KQuickConfigModule>
#include <KQuickConfigModuleLoader>

#include <QAbstractItemModel>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QProcess>
#include <QQuickWindow>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>

#include <functional>

using namespace Qt::StringLiterals;

class ArrangementViewTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanupTestCase();

    void testViewMenuHoldsItsItems();
    void testScreenContentsFollowBattery();
    void testDragSnaps();
    void testShiftDragDoesNotSnap();
    void testCtrlWheelZoomsAroundPointer();
    void testWheelWithoutCtrlDoesNotZoom();
    void testDragOnEmptyAreaPans();
    void testMiddleDragOverScreenPans();
    void testFitAll();
    void testScreenContentsShown();
    void testScreenContentsRefused();
    void testScreenContentsShowOneEye();
    void testFlipReachesKWin();
    void testPositionFieldsReachKWin();

private:
    struct Screen {
        QQuickItem *item = nullptr;
        int row = -1;
        QString name;
        QRectF rect;
    };
    QList<Screen> screens() const;
    Screen screenNamed(const QString &name) const;
    QPoint modelPosition(int row) const;
    QVariant evaluate(QQuickItem *item, const QString &expression) const;
    QObject *findObject(const QString &objectName) const;
    QPointF emptyPoint() const;
    void press(Qt::MouseButton button, Qt::KeyboardModifiers modifiers, QPointF at);
    void move(Qt::KeyboardModifiers modifiers, QPointF to);
    void release(Qt::MouseButton button, Qt::KeyboardModifiers modifiers, QPointF at);
    void wheel(QPointF at, int angle, Qt::KeyboardModifiers modifiers);
    void capture(const QString &name, const QJsonObject &expectation = {});
    void reload();
    QVariant modelValue(int row, const char *role) const;
    QJsonObject doctorOutput(const QString &name) const;
    void activate(QObject *control);
    void type(const QString &text);
    void apply();

    KQuickConfigModule *m_module = nullptr;
    std::unique_ptr<QQuickWindow> m_window;
    QQuickItem *m_view = nullptr;
    QString m_captureDir;
    QHash<QString, QColor> m_screenColors;
};

static QQuickItem *findItem(QQuickItem *root, const std::function<bool(QQuickItem *)> &match)
{
    if (match(root)) {
        return root;
    }
    const auto children = root->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *found = findItem(child, match)) {
            return found;
        }
    }
    return nullptr;
}

static void findItems(QQuickItem *root, const std::function<bool(QQuickItem *)> &match, QList<QQuickItem *> &found)
{
    if (match(root)) {
        found << root;
    }
    const auto children = root->childItems();
    for (QQuickItem *child : children) {
        findItems(child, match, found);
    }
}

static bool hasProperty(const QObject *object, const char *name)
{
    return object->metaObject()->indexOfProperty(name) >= 0;
}

static QJsonObject rectObject(const QRectF &rect)
{
    return QJsonObject{{u"x"_s, rect.x()}, {u"y"_s, rect.y()}, {u"width"_s, rect.width()}, {u"height"_s, rect.height()}};
}

void ArrangementViewTest::initTestCase()
{
    if (qEnvironmentVariableIsEmpty("KSCREEN_TEST_ARRANGEMENT")) {
        QSKIP("Needs a compositor with two or more screens: set KSCREEN_TEST_ARRANGEMENT");
    }
    m_captureDir = qEnvironmentVariable("KSCREEN_TEST_CAPTURE_DIR");
    if (!m_captureDir.isEmpty()) {
        QDir().mkpath(m_captureDir);
    }
    const auto colors = qEnvironmentVariable("KSCREEN_TEST_SCREEN_COLORS").split(u';', Qt::SkipEmptyParts);
    for (const QString &entry : colors) {
        const auto parts = entry.split(u'=');
        if (parts.size() == 2) {
            m_screenColors.insert(parts[0], QColor(parts[1]));
        }
    }

    const QString pluginFile = qEnvironmentVariable("KSCREEN_TEST_KCM");
    const auto metaData = pluginFile.isEmpty() ? KPluginMetaData::findPluginById(u"plasma/kcms/systemsettings"_s, u"kcm_kscreen"_s)
                                               : KPluginMetaData(pluginFile);
    QVERIFY2(metaData.isValid(), "kcm_kscreen not found");
    qInfo() << "KCM" << metaData.fileName();
    auto result = KQuickConfigModuleLoader::loadModule(metaData, this);
    QVERIFY2(result, qPrintable(result.errorText));
    m_module = result.plugin;

    QQuickItem *ui = m_module->mainUi();
    QVERIFY2(ui, qPrintable(m_module->errorString()));
    m_window = std::make_unique<QQuickWindow>();
    m_window->resize(1000, 720);
    ui->setParentItem(m_window->contentItem());
    ui->setSize(m_window->size());
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));

    m_module->load();
    QTRY_VERIFY_WITH_TIMEOUT(m_module->property("backendReady").toBool(), 15000);
    m_view = findItem(ui, [](QQuickItem *item) {
        return hasProperty(item, "relativeFactor") && hasProperty(item, "totalSize");
    });
    QVERIFY(m_view);
    QTRY_VERIFY_WITH_TIMEOUT(screens().size() >= 2, 10000);
}

void ArrangementViewTest::reload()
{
    m_module->load();
    QTRY_VERIFY_WITH_TIMEOUT(m_module->property("backendReady").toBool(), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(screens().size() >= 2, 10000);
}

void ArrangementViewTest::init()
{
    reload();
    if (m_view->metaObject()->indexOfMethod("fitAll()") >= 0) {
        QMetaObject::invokeMethod(m_view, "fitAll");
    }
    m_view->setProperty("interactive", true);
    // the view grows to its full height when it is edited
    QTest::qWait(600);
}

void ArrangementViewTest::cleanupTestCase()
{
    m_window.reset();
}

QVariant ArrangementViewTest::evaluate(QQuickItem *item, const QString &expression) const
{
    QQmlExpression qmlExpression(qmlContext(item), item, expression);
    return qmlExpression.evaluate();
}

QList<ArrangementViewTest::Screen> ArrangementViewTest::screens() const
{
    QList<QQuickItem *> items;
    findItems(m_view, [](QQuickItem *item) {
        return hasProperty(item, "outputSize") && hasProperty(item, "isSelected") && item->isVisible();
    }, items);
    QList<Screen> result;
    for (QQuickItem *item : std::as_const(items)) {
        Screen screen;
        screen.item = item;
        screen.row = evaluate(item, u"model.index"_s).toInt();
        screen.name = evaluate(item, u"model.name"_s).toString();
        screen.rect = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
        result << screen;
    }
    std::sort(result.begin(), result.end(), [](const Screen &a, const Screen &b) {
        return a.rect.x() < b.rect.x();
    });
    return result;
}

ArrangementViewTest::Screen ArrangementViewTest::screenNamed(const QString &name) const
{
    const auto all = screens();
    for (const Screen &screen : all) {
        if (screen.name == name) {
            return screen;
        }
    }
    return {};
}

QPoint ArrangementViewTest::modelPosition(int row) const
{
    auto model = m_module->property("outputModel").value<QAbstractItemModel *>();
    const int role = model->roleNames().key("position");
    return model->data(model->index(row, 0), role).toPoint();
}

QObject *ArrangementViewTest::findObject(const QString &objectName) const
{
    if (QObject *object = m_module->mainUi()->findChild<QObject *>(objectName)) {
        return object;
    }
    // Controls made by a Loader or a delegate hang from the scene, not from the QObject tree
    return findItem(m_module->mainUi(), [&objectName](QQuickItem *item) {
        return item->objectName() == objectName;
    });
}

QPointF ArrangementViewTest::emptyPoint() const
{
    // A point of the view that no screen covers, away from the heading and the buttons
    const QRectF view = m_view->mapRectToScene(QRectF(0, 0, m_view->width(), m_view->height()));
    const auto all = screens();
    for (qreal y = view.top() + view.height() * 0.35; y < view.bottom() - 60; y += 8) {
        for (qreal x = view.left() + 30; x < view.right() - 30; x += 8) {
            const QPointF point(x, y);
            if (std::none_of(all.cbegin(), all.cend(), [&point](const Screen &screen) {
                    return screen.rect.adjusted(-12, -12, 12, 12).contains(point);
                })) {
                return point;
            }
        }
    }
    return {};
}

void ArrangementViewTest::press(Qt::MouseButton button, Qt::KeyboardModifiers modifiers, QPointF at)
{
    QTest::mousePress(m_window.get(), button, modifiers, at.toPoint());
}

void ArrangementViewTest::move(Qt::KeyboardModifiers modifiers, QPointF to)
{
    QTest::mouseEvent(QTest::MouseMove, m_window.get(), Qt::NoButton, modifiers, to.toPoint());
    QTest::qWait(20);
}

void ArrangementViewTest::release(Qt::MouseButton button, Qt::KeyboardModifiers modifiers, QPointF at)
{
    QTest::mouseRelease(m_window.get(), button, modifiers, at.toPoint());
    QTest::qWait(100);
}

void ArrangementViewTest::wheel(QPointF at, int angle, Qt::KeyboardModifiers modifiers)
{
    QWheelEvent event(at, m_window->mapToGlobal(at), QPoint(), QPoint(0, angle), Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(m_window.get(), &event);
    QTest::qWait(100);
}

void ArrangementViewTest::capture(const QString &name, const QJsonObject &expectation)
{
    if (m_captureDir.isEmpty()) {
        return;
    }
    QTest::qWait(150);
    const QImage image = m_window->grabWindow();
    image.save(m_captureDir + u'/' + name + u".png"_s);

    QJsonArray screenArray;
    const auto all = screens();
    for (const Screen &screen : all) {
        const QPoint position = modelPosition(screen.row);
        const QSize size = evaluate(screen.item, u"model.size"_s).toSize();
        screenArray.append(QJsonObject{
            {u"name"_s, screen.name},
            {u"position"_s, QJsonArray{position.x(), position.y()}},
            {u"size"_s, QJsonArray{size.width(), size.height()}},
            {u"drawn"_s, rectObject(screen.rect)},
        });
    }
    const QRectF view = m_view->mapRectToScene(QRectF(0, 0, m_view->width(), m_view->height()));
    QJsonObject record{
        {u"image"_s, name + u".png"_s},
        {u"view"_s, rectObject(view)},
        {u"screens"_s, screenArray},
        {u"expectation"_s, expectation},
    };
    QFile file(m_captureDir + u'/' + name + u".json"_s);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(record).toJson());
}

QVariant ArrangementViewTest::modelValue(int row, const char *role) const
{
    auto model = m_module->property("outputModel").value<QAbstractItemModel *>();
    return model->data(model->index(row, 0), model->roleNames().key(role));
}

// What KWin holds for an output, as kscreen-doctor reads it from the compositor
QJsonObject ArrangementViewTest::doctorOutput(const QString &name) const
{
    QProcess doctor;
    doctor.start(u"kscreen-doctor"_s, {u"-j"_s});
    if (!doctor.waitForFinished(15000)) {
        return {};
    }
    const QJsonDocument document = QJsonDocument::fromJson(doctor.readAllStandardOutput());
    const QJsonArray outputs = document.object().value(u"outputs"_s).toArray();
    for (const QJsonValue &value : outputs) {
        if (value.toObject().value(u"name"_s).toString() == name) {
            return value.toObject();
        }
    }
    return {};
}

// Operates a control with the keyboard, as a person would
void ArrangementViewTest::activate(QObject *control)
{
    auto *item = qobject_cast<QQuickItem *>(control);
    QVERIFY(item);
    item->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(m_window.get(), Qt::Key_Space);
    QTest::qWait(100);
}

void ArrangementViewTest::type(const QString &text)
{
    for (const QChar character : text) {
        QTest::keyClick(m_window.get(), character.toLatin1());
    }
}

// Applies what the page holds to the compositor, and waits for the page to take KWin's answer
void ArrangementViewTest::apply()
{
    m_module->save();
    QTest::qWait(500);
}

void ArrangementViewTest::testViewMenuHoldsItsItems()
{
    // Zoom In, Zoom Out, Fit All, a separator and Show Screen Contents are the menu's own items,
    // not items nested inside one another
    QObject *contents = findObject(u"showScreenContentsAction"_s);
    QVERIFY2(contents, "no Show Screen Contents action in the arrangement view's menu");
    QObject *item = contents->parent();
    QVERIFY(item);
    auto *menu = item->property("menu").value<QObject *>();
    QVERIFY2(menu, "Show Screen Contents is not an item of the View menu");
    const int count = menu->property("count").toInt();
    QVERIFY2(count >= 5, qPrintable(u"the View menu holds %1 items"_s.arg(count)));
}

void ArrangementViewTest::testFlipReachesKWin()
{
    QObject *flip = findObject(u"flipCheckBox"_s);
    QVERIFY2(flip, "no flip control in the orientation settings");
    const QString name = evaluate(qobject_cast<QQuickItem *>(flip), u"element.name"_s).toString();
    QVERIFY(!name.isEmpty());
    int row = -1;
    for (const Screen &screen : screens()) {
        if (screen.name == name) {
            row = screen.row;
        }
    }
    QVERIFY(row >= 0);
    QCOMPARE(doctorOutput(name).value(u"rotation"_s).toInt(), 1);

    activate(flip);
    QCOMPARE(modelValue(row, "flipped").toBool(), true);
    // the screen in the view says so
    QList<QQuickItem *> indicators;
    findItems(m_view, [](QQuickItem *item) {
        return item->objectName() == u"flippedIndicator"_s;
    }, indicators);
    int shown = 0;
    for (QQuickItem *indicator : indicators) {
        shown += indicator->isVisible() ? 1 : 0;
    }
    QCOMPARE(shown, 1);
    capture(u"flipped-screen"_s, QJsonObject{{u"flipped"_s, name}});
    apply();
    QTRY_COMPARE_WITH_TIMEOUT(doctorOutput(name).value(u"rotation"_s).toInt(), 16, 10000);

    // turning the screen keeps the mirroring
    reload();
    flip = findObject(u"flipCheckBox"_s);
    QVERIFY(flip);
    QCOMPARE(flip->property("checked").toBool(), true);
    QCOMPARE(modelValue(row, "rotation").toInt(), 1);

    activate(flip);
    QCOMPARE(modelValue(row, "flipped").toBool(), false);
    apply();
    QTRY_COMPARE_WITH_TIMEOUT(doctorOutput(name).value(u"rotation"_s).toInt(), 1, 10000);
}

void ArrangementViewTest::testPositionFieldsReachKWin()
{
    QObject *xField = findObject(u"positionXField"_s);
    QObject *yField = findObject(u"positionYField"_s);
    QVERIFY2(xField && yField, "no X and Y fields for the position of the screen");
    const QString name = evaluate(qobject_cast<QQuickItem *>(xField), u"element.name"_s).toString();
    int row = -1;
    for (const Screen &screen : screens()) {
        if (screen.name == name) {
            row = screen.row;
        }
    }
    QVERIFY(row >= 0);
    // the fields hold the coordinates KWin holds
    const QJsonObject before = doctorOutput(name).value(u"pos"_s).toObject();
    QCOMPARE(xField->property("value").toInt(), before.value(u"x"_s).toInt());
    QCOMPARE(yField->property("value").toInt(), before.value(u"y"_s).toInt());

    // type 120 into Y
    auto *item = qobject_cast<QQuickItem *>(yField);
    item->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(m_window.get(), Qt::Key_A, Qt::ControlModifier);
    type(u"120"_s);
    QTest::keyClick(m_window.get(), Qt::Key_Return);
    QTest::qWait(100);
    QCOMPARE(modelValue(row, "normalizedPosition").toPoint().y(), 120);
    capture(u"position-typed"_s, QJsonObject{{u"moved"_s, name}, {u"y"_s, 120}});
    apply();
    QTRY_COMPARE_WITH_TIMEOUT(doctorOutput(name).value(u"pos"_s).toObject().value(u"y"_s).toInt(), 120, 10000);
    QCOMPARE(doctorOutput(name).value(u"pos"_s).toObject().value(u"x"_s).toInt(), before.value(u"x"_s).toInt());

    reload();
    yField = findObject(u"positionYField"_s);
    QCOMPARE(yField->property("value").toInt(), 120);
    item = qobject_cast<QQuickItem *>(yField);
    item->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(m_window.get(), Qt::Key_A, Qt::ControlModifier);
    type(QString::number(before.value(u"y"_s).toInt()));
    QTest::keyClick(m_window.get(), Qt::Key_Return);
    QTest::qWait(100);
    apply();
    QTRY_COMPARE_WITH_TIMEOUT(doctorOutput(name).value(u"pos"_s).toObject().value(u"y"_s).toInt(), before.value(u"y"_s).toInt(), 10000);
}

void ArrangementViewTest::testScreenContentsFollowBattery()
{
    if (qEnvironmentVariableIsEmpty("KSCREEN_TEST_UPOWER_MOCK")) {
        QSKIP("Needs a mock UPower on the system bus: set KSCREEN_TEST_UPOWER_MOCK");
    }
    QObject *action = findObject(u"showScreenContentsAction"_s);
    QVERIFY2(action, "no Show Screen Contents action in the arrangement view's menu");

    auto setOnBattery = [](bool onBattery) {
        QDBusMessage message = QDBusMessage::createMethodCall(u"org.freedesktop.UPower"_s,
                                                              u"/org/freedesktop/UPower"_s,
                                                              u"org.freedesktop.DBus.Properties"_s,
                                                              u"Set"_s);
        message << u"org.freedesktop.UPower"_s << u"OnBattery"_s << QVariant::fromValue(QDBusVariant(onBattery));
        const QDBusMessage reply = QDBusConnection::systemBus().call(message);
        QVERIFY2(reply.type() == QDBusMessage::ReplyMessage, qPrintable(reply.errorMessage()));
    };

    // Before the person chooses, the previews follow the power source
    setOnBattery(false);
    QTRY_VERIFY_WITH_TIMEOUT(action->property("checked").toBool(), 5000);
    setOnBattery(true);
    QTRY_VERIFY_WITH_TIMEOUT(!action->property("checked").toBool(), 5000);
    setOnBattery(false);
    QTRY_VERIFY_WITH_TIMEOUT(action->property("checked").toBool(), 5000);

    // Their choice wins over the power source
    QMetaObject::invokeMethod(action, "trigger");
    QTRY_VERIFY(!action->property("checked").toBool());
    setOnBattery(true);
    QTest::qWait(500);
    QVERIFY(!action->property("checked").toBool());
    setOnBattery(false);
    QTest::qWait(500);
    QVERIFY(!action->property("checked").toBool());

    QMetaObject::invokeMethod(action, "trigger");
    QTRY_VERIFY(action->property("checked").toBool());
    setOnBattery(true);
    QTest::qWait(500);
    QVERIFY(action->property("checked").toBool());
    setOnBattery(false);
}

void ArrangementViewTest::testDragSnaps()
{
    const auto all = screens();
    const Screen screen = all.last();
    const QPoint start = modelPosition(screen.row);
    const qreal factor = m_view->property("relativeFactor").toReal();
    // Lower the screen by a few pixels of the view, within the snapping distance of 80
    const int down = qMax(2, int(40 / factor));
    const QPointF from = screen.rect.center();
    press(Qt::LeftButton, Qt::NoModifier, from);
    move(Qt::NoModifier, from + QPointF(0, 15));
    move(Qt::NoModifier, from + QPointF(0, 30));
    move(Qt::NoModifier, from + QPointF(0, down));
    capture(u"snapping-on-during-drag"_s, QJsonObject{{u"dragged"_s, screen.name}, {u"down"_s, down}, {u"snapped"_s, true}});
    release(Qt::LeftButton, Qt::NoModifier, from + QPointF(0, down));
    QCOMPARE(modelPosition(screen.row), start);
}

void ArrangementViewTest::testShiftDragDoesNotSnap()
{
    const auto all = screens();
    const Screen screen = all.last();
    const QPoint start = modelPosition(screen.row);
    const qreal factor = m_view->property("relativeFactor").toReal();
    const int down = qMax(2, int(40 / factor));
    const QPointF from = screen.rect.center();
    press(Qt::LeftButton, Qt::ShiftModifier, from);
    move(Qt::ShiftModifier, from + QPointF(0, 15));
    move(Qt::ShiftModifier, from + QPointF(0, 30));
    move(Qt::ShiftModifier, from + QPointF(0, down));
    capture(u"snapping-off-shift-held"_s, QJsonObject{{u"dragged"_s, screen.name}, {u"down"_s, down}, {u"snapped"_s, false}});
    const QPoint during = modelPosition(screen.row);
    release(Qt::LeftButton, Qt::ShiftModifier, from + QPointF(0, down));
    const QPoint end = modelPosition(screen.row);
    qInfo() << "factor" << factor << "down" << down << "start" << start << "during" << during << "end" << end;
    QCOMPARE(end.x(), start.x());
    QVERIFY2(std::abs(end.y() - start.y() - down * factor) <= factor + 1,
             qPrintable(u"lowered by %1, expected %2"_s.arg(end.y() - start.y()).arg(down * factor)));
}

void ArrangementViewTest::testCtrlWheelZoomsAroundPointer()
{
    const auto before = screens();
    QList<QPoint> positions;
    for (const Screen &screen : before) {
        positions << modelPosition(screen.row);
    }
    const Screen first = before.first();
    // The pointer over a known point of the first screen, a quarter into it
    const QPointF at(first.rect.left() + first.rect.width() / 4, first.rect.center().y());
    capture(u"zoom-before"_s);
    wheel(at, 120, Qt::ControlModifier);
    const auto after = screens();
    QCOMPARE(after.size(), before.size());
    capture(u"zoomed-in-one-step"_s, QJsonObject{{u"zoomSteps"_s, 1}, {u"at"_s, QJsonArray{at.x(), at.y()}}, {u"step"_s, 1.25}});
    for (int i = 0; i < before.size(); ++i) {
        QVERIFY2(std::abs(after[i].rect.width() - before[i].rect.width() * 1.25) <= 1.5,
                 qPrintable(u"%1 is %2 wide, expected %3"_s.arg(after[i].name).arg(after[i].rect.width()).arg(before[i].rect.width() * 1.25)));
        // The point under the pointer stays under it
        const qreal expectedLeft = at.x() - (at.x() - before[i].rect.left()) * 1.25;
        const qreal expectedTop = at.y() - (at.y() - before[i].rect.top()) * 1.25;
        QVERIFY2(std::abs(after[i].rect.left() - expectedLeft) <= 1.5, qPrintable(u"left %1, expected %2"_s.arg(after[i].rect.left()).arg(expectedLeft)));
        QVERIFY2(std::abs(after[i].rect.top() - expectedTop) <= 1.5, qPrintable(u"top %1, expected %2"_s.arg(after[i].rect.top()).arg(expectedTop)));
    }
    wheel(at, -120, Qt::ControlModifier);
    const auto back = screens();
    for (int i = 0; i < before.size(); ++i) {
        QVERIFY(std::abs(back[i].rect.left() - before[i].rect.left()) <= 2);
        QVERIFY(std::abs(back[i].rect.width() - before[i].rect.width()) <= 2);
    }
    // Zooming changes no screen's place
    for (int i = 0; i < before.size(); ++i) {
        QCOMPARE(modelPosition(before[i].row), positions[i]);
    }
}

void ArrangementViewTest::testWheelWithoutCtrlDoesNotZoom()
{
    const auto before = screens();
    wheel(before.first().rect.center(), 120, Qt::NoModifier);
    const auto after = screens();
    for (int i = 0; i < before.size(); ++i) {
        QCOMPARE(after[i].rect.width(), before[i].rect.width());
    }
}

void ArrangementViewTest::testDragOnEmptyAreaPans()
{
    const auto before = screens();
    QList<QPoint> positions;
    for (const Screen &screen : before) {
        positions << modelPosition(screen.row);
    }
    const QPointF from = emptyPoint();
    QVERIFY2(!from.isNull(), "no empty point in the view");
    const QPointF delta(60, 30);
    press(Qt::LeftButton, Qt::NoModifier, from);
    for (int i = 1; i <= 6; ++i) {
        move(Qt::NoModifier, from + delta * i / 6);
    }
    release(Qt::LeftButton, Qt::NoModifier, from + delta);
    const auto after = screens();
    capture(u"panned-empty-area"_s, QJsonObject{{u"pan"_s, QJsonArray{delta.x(), delta.y()}}, {u"from"_s, QJsonArray{from.x(), from.y()}}});
    for (int i = 0; i < before.size(); ++i) {
        QVERIFY2(std::abs(after[i].rect.left() - before[i].rect.left() - delta.x()) <= 1,
                 qPrintable(u"%1 moved by %2, expected %3"_s.arg(after[i].name).arg(after[i].rect.left() - before[i].rect.left()).arg(delta.x())));
        QVERIFY(std::abs(after[i].rect.top() - before[i].rect.top() - delta.y()) <= 1);
        QCOMPARE(modelPosition(after[i].row), positions[i]);
    }
}

void ArrangementViewTest::testMiddleDragOverScreenPans()
{
    const auto before = screens();
    QList<QPoint> positions;
    for (const Screen &screen : before) {
        positions << modelPosition(screen.row);
    }
    const QPointF from = before.first().rect.center();
    const QPointF delta(-40, 25);
    press(Qt::MiddleButton, Qt::NoModifier, from);
    for (int i = 1; i <= 6; ++i) {
        move(Qt::NoModifier, from + delta * i / 6);
    }
    release(Qt::MiddleButton, Qt::NoModifier, from + delta);
    const auto after = screens();
    capture(u"panned-middle-button"_s, QJsonObject{{u"pan"_s, QJsonArray{delta.x(), delta.y()}}});
    for (int i = 0; i < before.size(); ++i) {
        QVERIFY2(std::abs(after[i].rect.left() - before[i].rect.left() - delta.x()) <= 1,
                 qPrintable(u"%1 moved by %2, expected %3"_s.arg(after[i].name).arg(after[i].rect.left() - before[i].rect.left()).arg(delta.x())));
        QVERIFY(std::abs(after[i].rect.top() - before[i].rect.top() - delta.y()) <= 1);
        QCOMPARE(modelPosition(after[i].row), positions[i]);
    }
}

void ArrangementViewTest::testFitAll()
{
    const auto fitted = screens();
    QObject *fitAll = findObject(u"fitAllAction"_s);
    QVERIFY2(fitAll, "no Fit All action in the arrangement view's menu");
    QObject *zoomIn = findObject(u"zoomInAction"_s);
    QVERIFY2(zoomIn, "no Zoom In action in the arrangement view's menu");

    QMetaObject::invokeMethod(zoomIn, "trigger");
    QMetaObject::invokeMethod(zoomIn, "trigger");
    const auto zoomed = screens();
    QVERIFY(std::abs(zoomed.first().rect.width() - fitted.first().rect.width() * 1.5625) <= 2);
    // The middle button pans from anywhere, over a screen too
    const QPointF from = zoomed.first().rect.center();
    press(Qt::MiddleButton, Qt::NoModifier, from);
    for (int i = 1; i <= 4; ++i) {
        move(Qt::NoModifier, from + QPointF(-20 * i, 10 * i));
    }
    release(Qt::MiddleButton, Qt::NoModifier, from + QPointF(-80, 40));
    const auto panned = screens();
    QVERIFY(std::abs(panned.first().rect.left() - zoomed.first().rect.left() + 80) <= 1);
    QVERIFY(std::abs(panned.first().rect.top() - zoomed.first().rect.top() - 40) <= 1);
    capture(u"zoomed-and-panned"_s, QJsonObject{{u"zoomSteps"_s, 2}, {u"menu"_s, true}, {u"pan"_s, QJsonArray{-80, 40}}});

    QMetaObject::invokeMethod(fitAll, "trigger");
    const auto back = screens();
    capture(u"fit-all"_s, QJsonObject{{u"fit"_s, true}});
    for (int i = 0; i < fitted.size(); ++i) {
        QVERIFY(std::abs(back[i].rect.left() - fitted[i].rect.left()) <= 1);
        QVERIFY(std::abs(back[i].rect.top() - fitted[i].rect.top()) <= 1);
        QVERIFY(std::abs(back[i].rect.width() - fitted[i].rect.width()) <= 1);
    }
}

static QColor medianColor(const QImage &image, const QRectF &rect)
{
    QList<int> reds, greens, blues;
    // a ring inside the screen's rectangle, clear of the labels in the middle
    for (qreal fy : {0.2, 0.8}) {
        for (qreal fx = 0.1; fx <= 0.9; fx += 0.05) {
            const QPoint point = QPointF(rect.left() + rect.width() * fx, rect.top() + rect.height() * fy).toPoint();
            if (!image.rect().contains(point)) {
                continue;
            }
            const QColor color = image.pixelColor(point);
            reds << color.red();
            greens << color.green();
            blues << color.blue();
        }
    }
    if (reds.isEmpty()) {
        return {};
    }
    std::sort(reds.begin(), reds.end());
    std::sort(greens.begin(), greens.end());
    std::sort(blues.begin(), blues.end());
    return QColor(reds[reds.size() / 2], greens[greens.size() / 2], blues[blues.size() / 2]);
}

static int colorDistance(const QColor &a, const QColor &b)
{
    return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue());
}

void ArrangementViewTest::testScreenContentsShown()
{
    if (m_screenColors.isEmpty() || !qEnvironmentVariableIsEmpty("KSCREEN_TEST_EXPECT_REFUSAL")) {
        QSKIP("Needs coloured screens and a compositor that allows captures: set KSCREEN_TEST_SCREEN_COLORS");
    }
    QObject *action = findObject(u"showScreenContentsAction"_s);
    QVERIFY2(action, "no Show Screen Contents action in the arrangement view's menu");

    if (action->property("checked").toBool()) {
        QMetaObject::invokeMethod(action, "trigger");
    }
    QTRY_VERIFY(!action->property("checked").toBool());
    QTest::qWait(1500);
    QImage image = m_window->grabWindow();
    for (auto it = m_screenColors.cbegin(); it != m_screenColors.cend(); ++it) {
        const Screen screen = screenNamed(it.key());
        QVERIFY2(screen.item, qPrintable(it.key()));
        const QColor seen = medianColor(image, screen.rect);
        qInfo() << "off" << it.key() << seen.name();
        QVERIFY2(colorDistance(seen, it.value()) > 120, qPrintable(u"%1 shows %2 with the previews off"_s.arg(it.key(), seen.name())));
    }
    QJsonObject colors;
    for (auto it = m_screenColors.cbegin(); it != m_screenColors.cend(); ++it) {
        colors.insert(it.key(), it.value().name());
    }
    capture(u"previews-off"_s, QJsonObject{{u"previews"_s, false}, {u"colors"_s, colors}});

    QMetaObject::invokeMethod(action, "trigger");
    QTRY_VERIFY(action->property("checked").toBool());
    // One capture per screen and second; wait for at least one
    QElapsedTimer timer;
    timer.start();
    bool allShown = false;
    while (!allShown && timer.elapsed() < 8000) {
        QTest::qWait(250);
        image = m_window->grabWindow();
        allShown = true;
        for (auto it = m_screenColors.cbegin(); it != m_screenColors.cend(); ++it) {
            allShown = allShown && colorDistance(medianColor(image, screenNamed(it.key()).rect), it.value()) <= 60;
        }
    }
    if (QObject *contents = m_module->property("screenContents").value<QObject *>()) {
        qInfo() << "screen contents: shown" << contents->property("shown") << "active" << contents->property("active") << "error"
                << contents->property("errorString");
    }
    for (auto it = m_screenColors.cbegin(); it != m_screenColors.cend(); ++it) {
        const QColor seen = medianColor(image, screenNamed(it.key()).rect);
        qInfo() << "on" << it.key() << seen.name() << "after" << timer.elapsed() << "ms";
        QVERIFY2(colorDistance(seen, it.value()) <= 60, qPrintable(u"%1 shows %2, expected %3"_s.arg(it.key(), seen.name(), it.value().name())));
    }
    capture(u"previews-on"_s, QJsonObject{{u"previews"_s, true}, {u"colors"_s, colors}});

    // The view keeps answering while the previews run: the longest pause of a 10 ms timer over three seconds;
    // and the captures leave no file descriptor behind
    const auto openFiles = [] {
        return QDir(u"/proc/self/fd"_s).entryList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::System).size();
    };
    const qsizetype filesBefore = openFiles();
    qint64 longest = 0;
    QElapsedTimer gap;
    gap.start();
    QTimer tick;
    tick.setInterval(10);
    connect(&tick, &QTimer::timeout, this, [&] {
        longest = std::max(longest, gap.restart());
    });
    tick.start();
    QTest::qWait(3000);
    tick.stop();
    qInfo() << "longest pause of the event loop with previews on:" << longest << "ms";
    QVERIFY2(longest < 250, qPrintable(u"the event loop stopped for %1 ms"_s.arg(longest)));
    const qsizetype filesAfter = openFiles();
    qInfo() << "open files before and after three seconds of captures:" << filesBefore << filesAfter;
    QVERIFY2(filesAfter <= filesBefore + 2, qPrintable(u"%1 files open, %2 before"_s.arg(filesAfter).arg(filesBefore)));

    // Zoomed in, the previews follow the screens
    QObject *zoomIn = findObject(u"zoomInAction"_s);
    QVERIFY(zoomIn);
    QMetaObject::invokeMethod(zoomIn, "trigger");
    QTest::qWait(1500);
    capture(u"previews-on-zoomed"_s, QJsonObject{{u"previews"_s, true}, {u"colors"_s, colors}, {u"zoomSteps"_s, 1}, {u"menu"_s, true}});
}

void ArrangementViewTest::testScreenContentsRefused()
{
    if (qEnvironmentVariableIsEmpty("KSCREEN_TEST_EXPECT_REFUSAL")) {
        QSKIP("Needs a compositor that refuses captures: set KSCREEN_TEST_EXPECT_REFUSAL");
    }
    QObject *action = findObject(u"showScreenContentsAction"_s);
    QVERIFY2(action, "no Show Screen Contents action in the arrangement view's menu");
    if (!action->property("checked").toBool()) {
        QMetaObject::invokeMethod(action, "trigger");
    }
    QObject *contents = m_module->property("screenContents").value<QObject *>();
    QVERIFY(contents);
    QTRY_VERIFY_WITH_TIMEOUT(!contents->property("errorString").toString().isEmpty(), 5000);
    qInfo() << "refused:" << contents->property("errorString").toString();
    capture(u"previews-refused"_s, QJsonObject{{u"previews"_s, true}, {u"refused"_s, true}});

    // The arrangement still works
    const auto before = screens();
    const QPointF from = emptyPoint();
    QVERIFY2(!from.isNull(), "no empty point in the view");
    press(Qt::LeftButton, Qt::NoModifier, from);
    for (int i = 1; i <= 4; ++i) {
        move(Qt::NoModifier, from + QPointF(10 * i, 0));
    }
    release(Qt::LeftButton, Qt::NoModifier, from + QPointF(40, 0));
    QVERIFY(std::abs(screens().first().rect.left() - before.first().rect.left() - 40) <= 1);
}

void ArrangementViewTest::testScreenContentsShowOneEye()
{
    const QString name = qEnvironmentVariable("KSCREEN_TEST_STEREO_SCREEN");
    if (name.isEmpty() || !qEnvironmentVariableIsEmpty("KSCREEN_TEST_EXPECT_REFUSAL")) {
        QSKIP("Needs a screen in a side by side 3D mode, red on its left half and blue on its right: set KSCREEN_TEST_STEREO_SCREEN");
    }
    QObject *action = findObject(u"showScreenContentsAction"_s);
    QVERIFY2(action, "no Show Screen Contents action in the arrangement view's menu");
    if (!action->property("checked").toBool()) {
        QMetaObject::invokeMethod(action, "trigger");
    }

    // Stereo KWin captures a screen in a 3D mode as both eyes side by side; the flat view shows one of them,
    // as wide as the screen: red on the left half and blue on the right, where both eyes squeezed into the
    // screen's rectangle would show red at 70 % of its width
    const QColor red(0xc0, 0, 0);
    const QColor blue(0, 0, 0xc0);
    QColor left;
    QColor right;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 8000) {
        QTest::qWait(250);
        const QImage image = m_window->grabWindow();
        const QRectF rect = screenNamed(name).rect;
        left = image.pixelColor(QPointF(rect.left() + rect.width() * 0.2, rect.top() + rect.height() * 0.2).toPoint());
        right = image.pixelColor(QPointF(rect.left() + rect.width() * 0.7, rect.top() + rect.height() * 0.2).toPoint());
        if (colorDistance(left, red) <= 60 && colorDistance(right, blue) <= 60) {
            break;
        }
    }
    qInfo() << name << "shows" << left.name() << "at 20 % and" << right.name() << "at 70 % of its width";
    capture(u"previews-on-stereo-screen"_s, QJsonObject{{u"previews"_s, true}, {u"stereoScreen"_s, name}});
    QVERIFY2(colorDistance(left, red) <= 60, qPrintable(u"%1 shows %2 at 20 %, expected red"_s.arg(name, left.name())));
    QVERIFY2(colorDistance(right, blue) <= 60, qPrintable(u"%1 shows %2 at 70 %, expected blue"_s.arg(name, right.name())));
}

QTEST_MAIN(ArrangementViewTest)

#include "arrangementview.moc"
