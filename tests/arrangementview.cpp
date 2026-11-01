/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Drives the arrangement view of the installed or the freshly built KCM, inside a running
// compositor with two or more screens: snapping and Shift.
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
    void press(Qt::MouseButton button, Qt::KeyboardModifiers modifiers, QPointF at);
    void move(Qt::KeyboardModifiers modifiers, QPointF to);
    void release(Qt::MouseButton button, Qt::KeyboardModifiers modifiers, QPointF at);
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

QTEST_MAIN(ArrangementViewTest)

#include "arrangementview.moc"
