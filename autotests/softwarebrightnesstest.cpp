// SPDX-License-Identifier: LGPL-2.0-only

#include "controllers/softwarebrightness.h"
#include <powerdevil_debug.h>

#include <QDBusConnection>
#include <QSignalSpy>
#include <QTest>

Q_LOGGING_CATEGORY(POWERDEVIL, "org.kde.powerdevil")

class FakeBrightnessService : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.sonicde.SoftwareBrightness")
public:
    QVariantMap outputs;
    bool accept = true;
    int calls = 0;
    int lastBrightness = 0;
    double lastDimming = 0.0;

public Q_SLOTS:
    Q_SCRIPTABLE QVariantMap Displays() const
    {
        return outputs;
    }
    Q_SCRIPTABLE bool SetBrightness(const QString &name, int value, double dimming)
    {
        ++calls;
        lastBrightness = value;
        lastDimming = dimming;
        if (!accept || !outputs.contains(name)) {
            return false;
        }
        auto info = outputs.value(name).toMap();
        info.insert(QStringLiteral("brightness"), value);
        outputs[name] = info;
        Q_EMIT BrightnessChanged(name, value);
        return true;
    }
Q_SIGNALS:
    Q_SCRIPTABLE void DisplaysChanged();
    Q_SCRIPTABLE void BrightnessChanged(const QString &name, int value);
};

class FakeHardware : public DisplayBrightness {
public:
    using DisplayBrightness::DisplayBrightness;
    QString id() const override
    {
        return QStringLiteral("hardware");
    }
    QString label() const override
    {
        return id();
    }
    int knownSafeMinBrightness() const override
    {
        return 1;
    }
    int maxBrightness() const override
    {
        return 100;
    }
    int brightness() const override
    {
        return 50;
    }
    void setBrightness(int, bool) override
    {
    }
    bool isInternal() const override
    {
        return internal;
    }
    std::optional<QByteArray> edidData() const override
    {
        return edid;
    }
    bool internal = false;
    std::optional<QByteArray> edid;
};

class SoftwareBrightnessTest : public QObject {
    Q_OBJECT
private:
    static QVariantMap info(int brightness = 10000, bool internal = false, const QByteArray &edid = {})
    {
        return {{QStringLiteral("brightness"), brightness},
                {QStringLiteral("label"), QStringLiteral("Test monitor")},
                {QStringLiteral("internal"), internal},
                {QStringLiteral("edid"), edid}};
    }

private Q_SLOTS:
    void realCompositor()
    {
        if (!qEnvironmentVariableIsSet("SOFTWARE_BRIGHTNESS_CLIENT_TEST")) {
            QSKIP("Run by the isolated SonicWin X11 integration test when a live service is available");
        }
        SoftwareBrightnessDetector detector(nullptr);
        QSignalSpy finished(&detector, &DisplayBrightnessDetector::detectionFinished);
        detector.detect();
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!detector.displays().isEmpty());
        auto display = detector.displays().first();
        display->setBrightness(4200);
        display->setDimmingMultiplier(0.5);
        QTRY_COMPARE(display->brightness(), 4200);
        // A read through a fresh detector must observe the server's value, not
        // merely the first client's optimistic local cache.
        SoftwareBrightnessDetector observer(nullptr);
        QSignalSpy observed(&observer, &DisplayBrightnessDetector::detectionFinished);
        observer.detect();
        QTRY_COMPARE(observed.count(), 1);
        QVERIFY(!observer.displays().isEmpty());
        QCOMPARE(observer.displays().first()->brightness(), 4200);
    }

    void noService()
    {
        SoftwareBrightnessDetector detector(nullptr);
        QSignalSpy finished(&detector, &DisplayBrightnessDetector::detectionFinished);
        detector.detect();
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!finished.first().first().toBool());
        QVERIFY(detector.displays().isEmpty());
    }

    void hardwareTakesPrecedence()
    {
        SoftwareBrightness software(QStringLiteral("HDMI-1"), info(5000, false, QByteArray(256, 'a')), this);
        FakeHardware hardware;
        QVERIFY(needsSoftwareBrightness(&software, {&hardware}));
        hardware.edid = QByteArray(128, 'a'); // DDC may report only the EDID base block.
        QVERIFY(!needsSoftwareBrightness(&software, {&hardware}));
        hardware.edid = QByteArray(128, 'b');
        QVERIFY(needsSoftwareBrightness(&software, {&hardware}));
        SoftwareBrightness internal(QStringLiteral("eDP-1"), info(5000, true), this);
        hardware.internal = true;
        QVERIFY(!needsSoftwareBrightness(&internal, {&hardware}));
    }

    void discoveryChangesFailureAndRestart()
    {
        auto bus = QDBusConnection::sessionBus();
        SoftwareBrightnessDetector detector(nullptr);
        QSignalSpy finished(&detector, &DisplayBrightnessDetector::detectionFinished);
        detector.detect();
        QTRY_COMPARE(finished.count(), 1);

        FakeBrightnessService service;
        service.outputs.insert(QStringLiteral("HDMI-1"), info());
        QVERIFY(bus.registerObject(QString::fromLatin1(SoftwareBrightnessDBus::path),
                                   &service,
                                   QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals));
        QVERIFY(bus.registerService(QString::fromLatin1(SoftwareBrightnessDBus::service)));
        QTRY_COMPARE(detector.displays().size(), 1);
        auto display = detector.displays().first();
        QTRY_VERIFY(service.calls >= 1); // clears temporary dimming after daemon restart
        QCOMPARE(display->brightness(), 10000);
        QCOMPARE(display->knownSafeMinBrightness(), 100);
        QCOMPARE(display->maxBrightness(), 10000);
        QVERIFY(display->supportsDimmingMultiplier());
        display->setBrightness(4200);
        display->setDimmingMultiplier(0.5);
        QTRY_COMPARE(service.lastBrightness, 4200);
        QTRY_COMPARE(service.lastDimming, 0.5);
        QCOMPARE(display->brightness(), 4200);
        QTest::qWait(20);
        Q_EMIT service.BrightnessChanged(QStringLiteral("HDMI-1"), 6000);
        QTRY_COMPARE(display->brightness(), 6000);
        auto data = service.outputs.value(QStringLiteral("HDMI-1")).toMap();
        data[QStringLiteral("brightness")] = 6000;
        service.outputs[QStringLiteral("HDMI-1")] = data;

        QSignalSpy observed(display, &DisplayBrightness::externalBrightnessChangeObserved);
        service.accept = false;
        display->setBrightness(2000);
        QTRY_VERIFY(!observed.isEmpty());
        QCOMPARE(display->brightness(), 6000); // failed writes do not leave a lying slider
        service.accept = true;

        service.outputs.insert(QStringLiteral("DP-1"), info(7000));
        Q_EMIT service.DisplaysChanged();
        QTRY_COMPARE(detector.displays().size(), 2);
        service.outputs.remove(QStringLiteral("HDMI-1"));
        Q_EMIT service.DisplaysChanged();
        QTRY_COMPARE(detector.displays().size(), 1);
        QCOMPARE(detector.displays().first()->id(), QStringLiteral("DP-1"));

        QVERIFY(bus.unregisterService(QString::fromLatin1(SoftwareBrightnessDBus::service)));
        QTRY_VERIFY(detector.displays().isEmpty());
        QVERIFY(bus.registerService(QString::fromLatin1(SoftwareBrightnessDBus::service)));
        QTRY_COMPARE(detector.displays().size(), 1);
        QVERIFY(bus.unregisterService(QString::fromLatin1(SoftwareBrightnessDBus::service)));
        bus.unregisterObject(QString::fromLatin1(SoftwareBrightnessDBus::path));
        QTRY_VERIFY(detector.displays().isEmpty());
    }
};

QTEST_GUILESS_MAIN(SoftwareBrightnessTest)
#include "softwarebrightnesstest.moc"
