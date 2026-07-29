#include "settings.h"

#include <QTemporaryDir>
#include <QtTest>

class TestSettings : public QObject {
  Q_OBJECT
 private slots:
  void defaultsRoundTripClamp();
  void migratesFormerBareHostDefault();
};

void TestSettings::defaultsRoundTripClamp() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString ini = dir.path() + "/cwb.ini";

  {
    CwbSettings s(ini);
    // defaults
    QCOMPARE(s.zoom(), 1.0);
    QCOMPARE(s.cacheMaxBytes(), 128LL * 1024 * 1024);
    QCOMPARE(s.homePage(), CwbSettings::defaultHomePage());
    QCOMPARE(CwbSettings::defaultHomePage(),
             QString("ces://ces.pubcom.org/"));
    // round-trip
    s.setZoom(1.5);
    s.setCacheMaxBytes(64LL * 1024 * 1024);
    s.setHomePage("file://h/s/x.html");
    // clamp on write + read
    s.setZoom(9.0);
    QCOMPARE(s.zoom(), 3.0);
    s.setZoom(0.01);
    QCOMPARE(s.zoom(), 0.5);
    s.setZoom(1.25);
    // non-positive cache falls back to default
    s.setCacheMaxBytes(0);
    QCOMPARE(s.cacheMaxBytes(), 128LL * 1024 * 1024);
    s.setCacheMaxBytes(64LL * 1024 * 1024);
    // per-host zoom: falls back to the global default until set
    s.setZoom(1.1);
    QCOMPARE(s.zoomForHost("a.example"), 1.1);  // no override -> default
    s.setZoomForHost("a.example", 2.0);
    QCOMPARE(s.zoomForHost("a.example"), 2.0);
    QCOMPARE(s.zoomForHost("b.example"), 1.1);  // other host still default
    s.setZoom(1.25);
    // session restore
    QCOMPARE(s.restoreSession(), false);
    QVERIFY(s.lastUrl().isEmpty());
    s.setRestoreSession(true);
    s.setLastUrl("file://x/s/last.html");
  }
  {  // persisted across reopen
    CwbSettings s2(ini);
    QCOMPARE(s2.zoom(), 1.25);
    QCOMPARE(s2.cacheMaxBytes(), 64LL * 1024 * 1024);
    QCOMPARE(s2.homePage(), QString("file://h/s/x.html"));
    QCOMPARE(s2.restoreSession(), true);
    QCOMPARE(s2.lastUrl(), QString("file://x/s/last.html"));
  }
}

void TestSettings::migratesFormerBareHostDefault() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  CwbSettings s(dir.path() + "/cwb.ini");
  s.setHomePage("ces.pubcom.org");
  QCOMPARE(s.homePage(), QString("ces://ces.pubcom.org/"));
}

QTEST_MAIN(TestSettings)
#include "test_settings.moc"
