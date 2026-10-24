/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <QFile>
#include <QTest>

// The reflection of a mirror-rig pair is named by what the picture does, not by an axis: a side
// mirror flips left to right, a beam splitter above flips top to bottom (Daniel, 24 Oct 2026).
// The labels live in the panel's QML, so the test reads it.
class PairLabelsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void reflectionLabelsSayWhatThePictureDoes()
    {
        QFile qml(QFINDTESTDATA("../kcm/ui/OutputPanel.qml"));
        QVERIFY(qml.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(qml.readAll());
        const int reflection = text.indexOf(QStringLiteral("Reflection:"));
        QVERIFY(reflection >= 0);
        const int end = text.indexOf(QStringLiteral("onActivated"), reflection);
        QVERIFY(end > reflection);
        const QString combo = text.mid(reflection, end - reflection);
        QVERIFY(combo.contains(QStringLiteral("i18n(\"None\"), value: KScreen.Output.StereoPairReflection.None")));
        QVERIFY(combo.contains(QStringLiteral("i18n(\"Flip left to right (side mirror)\"), value: KScreen.Output.StereoPairReflection.Horizontal")));
        QVERIFY(combo.contains(QStringLiteral("i18n(\"Flip top to bottom (beam splitter above)\"), value: KScreen.Output.StereoPairReflection.Vertical")));
        QVERIFY(!combo.contains(QStringLiteral("i18n(\"Horizontal\")")));
        QVERIFY(!combo.contains(QStringLiteral("i18n(\"Vertical\")")));
    }
};

QTEST_GUILESS_MAIN(PairLabelsTest)

#include "pairlabels.moc"
