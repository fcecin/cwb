#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "cache.h"

// Backdate an entry's LRU stamp so eviction order is deterministic in-test.
// Returns false if the entry does not exist (a ReadWrite open would silently
// CREATE it and mask a rejected put -- the caller must QVERIFY the result).
static bool backdate(const QString& dir, const QString& url, int secsAgo) {
  const QByteArray h =
      QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha256);
  QFile f(dir + "/" + QString::fromLatin1(h.toHex()));
  if (!f.exists()) return false;
  if (!f.open(QIODevice::ReadWrite)) return false;
  return f.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-secsAgo),
                       QFileDevice::FileModificationTime);
}

class CacheTest : public QObject {
  Q_OBJECT
 private slots:
  void roundtrip() {
    QTemporaryDir td;
    cwb::DiskCache c(td.path(), 4096);
    QVERIFY(!c.get("file://h:1/a").has_value());  // miss on absent
    c.put("file://h:1/a", "hello");
    auto got = c.get("file://h:1/a");
    QVERIFY(got.has_value());
    QCOMPARE(*got, QByteArray("hello"));
    c.put("file://h:1/a", "hello2");  // overwrite
    QCOMPARE(*c.get("file://h:1/a"), QByteArray("hello2"));
  }

  void distinctUrlsDistinctEntries() {
    QTemporaryDir td;
    cwb::DiskCache c(td.path(), 4096);
    c.put("file://h:1/a", "A");
    c.put("file://h:1/b", "B");
    QCOMPARE(*c.get("file://h:1/a"), QByteArray("A"));
    QCOMPARE(*c.get("file://h:1/b"), QByteArray("B"));
    QCOMPARE(c.totalBytes(), 2);
  }

  // Entries must respect the per-entry cap (maxBytes/8), so overflowing the
  // budget honestly takes 9 entries of 120 bytes against a 1000-byte cache.
  void evictsOldestWhenOverBudget() {
    QTemporaryDir td;
    cwb::DiskCache c(td.path(), 1000);
    for (int i = 1; i <= 8; ++i) {
      const QString url = QStringLiteral("u%1").arg(i);
      c.put(url, QByteArray(120, 'x'));
      // u1 oldest ... u8 newest, all older than the upcoming u9.
      QVERIFY(backdate(td.path(), url, 1000 - i * 100));
    }
    c.put("u9", QByteArray(120, 'z'));  // 1080 > 1000: u1 must go
    QVERIFY(!c.get("u1").has_value());
    for (int i = 2; i <= 9; ++i)
      QVERIFY(c.get(QStringLiteral("u%1").arg(i)).has_value());
    QVERIFY(c.totalBytes() <= 1000);
  }

  void getRefreshesLruStamp() {
    QTemporaryDir td;
    cwb::DiskCache c(td.path(), 1000);
    for (int i = 1; i <= 8; ++i) {
      const QString url = QStringLiteral("u%1").arg(i);
      c.put(url, QByteArray(120, 'x'));
      QVERIFY(backdate(td.path(), url, 1000 - i * 100));
    }
    QVERIFY(c.get("u1").has_value());   // touch u1: now u2 is the LRU entry
    c.put("u9", QByteArray(120, 'z'));  // eviction should take u2, not u1
    QVERIFY(c.get("u1").has_value());
    QVERIFY(!c.get("u2").has_value());
    QVERIFY(c.get("u9").has_value());
  }

  void oversizedEntryNotCached() {
    QTemporaryDir td;
    cwb::DiskCache c(td.path(), 800);  // per-entry cap = 100
    c.put("big", QByteArray(200, 'x'));
    QVERIFY(!c.get("big").has_value());
    QCOMPARE(c.totalBytes(), 0);
  }

  void clearEmptiesTheCache() {
    QTemporaryDir td;
    cwb::DiskCache c(td.path(), 4096);
    c.put("a", "1");
    c.put("b", "2");
    c.clear();
    QCOMPARE(c.totalBytes(), 0);
    QVERIFY(!c.get("a").has_value());
  }
};

QTEST_GUILESS_MAIN(CacheTest)
#include "test_cache.moc"
