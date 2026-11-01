/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Drives the arrangement view of the installed or the freshly built KCM, inside a running
// compositor with two or more screens: snapping and Shift, zoom, pan and Fit All.
//
// It runs only when KSCREEN_TEST_ARRANGEMENT is set, because it needs a compositor.
// KSCREEN_TEST_KCM: the KCM plugin file to load; without it, the one found on the plugin path
// KSCREEN_TEST_CAPTURE_DIR: where to write pictures of the view, with what they should show

#include <KPluginMetaData>
#include <KQuickConfigModule>
#include <KQuickConfigModuleLoader>

#include <QAbstractItemModel>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
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

    void testDragSnaps();
    void testShiftDragDoesNotSnap();
    void testCtrlWheelZoomsAroundPointer();
    void testWheelWithoutCtrlDoesNotZoom();
    void testDragOnEmptyAreaPans();
    void testMiddleDragOverScreenPans();
    void testFitAll();

private:
    struct Screen {
        QQuickItem *item = nullptr;
        int row = -1;
        QString name;
        QRectF rect;
    };
    QList<Screen> screens() const;
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

    KQuickConfigModule *m_module = nullptr;
    std::unique_ptr<QQuickWindow> m_window;
    QQuickItem *m_view = nullptr;
    QString m_captureDir;
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

QPoint ArrangementViewTest::modelPosition(int row) const
{
    auto model = m_module->property("outputModel").value<QAbstractItemModel *>();
    const int role = model->roleNames().key("position");
    return model->data(model->index(row, 0), role).toPoint();
}

QObject *ArrangementViewTest::findObject(const QString &objectName) const
{
    return m_module->mainUi()->findChild<QObject *>(objectName);
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

QTEST_MAIN(ArrangementViewTest)

#include "arrangementview.moc"
