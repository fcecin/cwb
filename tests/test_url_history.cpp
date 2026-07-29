#include "url_history.h"

#include <QTemporaryDir>
#include <QtTest>

class TestUrlHistory : public QObject {
  Q_OBJECT
 private slots:
  void orderDedupPersistCap();
};

void TestUrlHistory::orderDedupPersistCap() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.path() + "/history.txt";

  {
    UrlHistory h(path);
    QVERIFY(h.entries().isEmpty());
    h.add("file://a/1");
    h.add("file://b/2");
    h.add("file://c/3");
    // most-recent-first
    QCOMPARE(h.entries().value(0), QString("file://c/3"));
    QCOMPARE(h.entries().value(2), QString("file://a/1"));
    // case-insensitive dedup moves the entry to the front, no duplicate
    h.add("FILE://A/1");
    QCOMPARE(h.entries().size(), 3);
    QCOMPARE(h.entries().value(0), QString("FILE://A/1"));
    // blank is ignored
    h.add("   ");
    QCOMPARE(h.entries().size(), 3);
  }
  {  // persistence across reopen
    UrlHistory h2(path);
    QCOMPARE(h2.entries().size(), 3);
    QCOMPARE(h2.entries().value(0), QString("FILE://A/1"));
  }
  {  // cap: keep the newest cap() entries
    UrlHistory h3(path);
    for (int i = 0; i < UrlHistory::cap() + 50; ++i)
      h3.add(QString("file://x/%1").arg(i));
    QCOMPARE(h3.entries().size(), UrlHistory::cap());
    QCOMPARE(h3.entries().value(0),
             QString("file://x/%1").arg(UrlHistory::cap() + 49));
  }
  {  // remove + contains (case-insensitive)
    UrlHistory h(dir.path() + "/bm.txt");
    h.add("file://a/1");
    h.add("file://b/2");
    QVERIFY(h.contains("FILE://A/1"));
    h.remove("file://A/1");
    QVERIFY(!h.contains("file://a/1"));
    QCOMPARE(h.entries().size(), 1);
    QCOMPARE(h.entries().value(0), QString("file://b/2"));
    UrlHistory h2(dir.path() + "/bm.txt");  // removal persisted
    QCOMPARE(h2.entries().size(), 1);
  }
  {  // clear
    UrlHistory h4(path);
    QVERIFY(!h4.entries().isEmpty());
    h4.clear();
    QVERIFY(h4.entries().isEmpty());
    UrlHistory h5(path);
    QVERIFY(h5.entries().isEmpty());  // file was removed
  }
}

QTEST_MAIN(TestUrlHistory)
#include "test_url_history.moc"
