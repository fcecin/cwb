#include "names.h"

#include <QtTest>

class TestNames : public QObject {
  Q_OBJECT
 private slots:
  void keyNames();
  void pretty();
  void storyNames();
};

// canonicalKeyName mirrors the server: spaces<->underscores, boundary stripped,
// then validated. It returns the UNDERSCORE (stored / path) form.
void TestNames::keyNames() {
  QCOMPARE(cwb::canonicalKeyName("  Ada Lovelace  "), QString("Ada_Lovelace"));
  QCOMPARE(cwb::canonicalKeyName("Ada_Lovelace"), QString("Ada_Lovelace"));
  QCOMPARE(cwb::canonicalKeyName("_Ada_"), QString("Ada"));   // trim underscores
  QCOMPARE(cwb::canonicalKeyName("ada"), QString("ada"));
  QCOMPARE(cwb::canonicalKeyName("fcecin-42"), QString("fcecin-42"));
  QCOMPARE(cwb::canonicalKeyName(QString::fromUtf8("Fabi\xC3\xA1na")),
           QString::fromUtf8("Fabi\xC3\xA1na"));
  // Space form and underscore form yield the SAME canonical name.
  QCOMPARE(cwb::canonicalKeyName("a b"), cwb::canonicalKeyName("a_b"));
  QCOMPARE(cwb::canonicalKeyName("a b"), QString("a_b"));
  // Rejections.
  QCOMPARE(cwb::canonicalKeyName(""), QString());
  QCOMPARE(cwb::canonicalKeyName("   "), QString());           // all boundary
  QCOMPARE(cwb::canonicalKeyName("__ __"), QString());
  QCOMPARE(cwb::canonicalKeyName(QString("a\nb")), QString());  // control char
  QCOMPARE(cwb::canonicalKeyName("a/b"), QString());            // path breaker
  QCOMPARE(cwb::canonicalKeyName("a:b"), QString());
  QCOMPARE(cwb::canonicalKeyName("a#b"), QString());
  QCOMPARE(cwb::canonicalKeyName(".hidden"), QString());   // leading dot
  QCOMPARE(cwb::canonicalKeyName("-opt"), QString());      // leading dash
  // 32 bytes fit; 33 do not; multibyte counts in BYTES.
  QCOMPARE(cwb::canonicalKeyName(QString(32, 'x')), QString(32, 'x'));
  QCOMPARE(cwb::canonicalKeyName(QString(33, 'x')), QString());
  const QChar e(0x00E9);  // e-acute, 2 UTF-8 bytes
  QCOMPARE(cwb::canonicalKeyName(QString(16, e)), QString(16, e));
  QCOMPARE(cwb::canonicalKeyName(QString(17, e)), QString());
}

// prettyName is the display form: underscores rendered back as spaces.
void TestNames::pretty() {
  QCOMPARE(cwb::prettyName("Ada_Lovelace"), QString("Ada Lovelace"));
  QCOMPARE(cwb::prettyName("fabiana"), QString("fabiana"));
  QCOMPARE(cwb::prettyName("a_b_c"), QString("a b c"));
  // Round-trip: canonical then pretty recovers the spaced display.
  QCOMPARE(cwb::prettyName(cwb::canonicalKeyName("Ada Lovelace")),
           QString("Ada Lovelace"));
}

void TestNames::storyNames() {
  const QString n = cwb::storyFileName(
      QStringLiteral("The Server That Lists You!"), QByteArray("body"));
  QVERIFY(n.startsWith(QStringLiteral("the-server-that-lists-you-")));
  QVERIFY(n.endsWith(QStringLiteral(".html")));
  // kebab + '-' + 6 hex + .html
  const QString suffix = n.section('-', -1).section('.', 0, 0);
  QCOMPARE(suffix.size(), 6);
  // Same title+content -> same name; different content -> different name.
  QCOMPARE(cwb::storyFileName(QStringLiteral("The Server That Lists You!"),
                              QByteArray("body")),
           n);
  QVERIFY(cwb::storyFileName(QStringLiteral("The Server That Lists You!"),
                             QByteArray("other")) != n);
  // Empty/exotic titles never produce an empty stem.
  QVERIFY(cwb::storyFileName(QStringLiteral("!!!"), QByteArray("x"))
              .startsWith(QStringLiteral("untitled-")));
}

QTEST_APPLESS_MAIN(TestNames)
#include "test_names.moc"
