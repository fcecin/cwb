#include "credits.h"

#include <QtTest>

class TestCredits : public QObject {
  Q_OBJECT
 private slots:
  void textNotation();
  void parseExact();
  void parseRejects();
};

void TestCredits::textNotation() {
  QCOMPARE(cwb::creditsText(0), QStringLiteral("0.00000000"));
  QCOMPARE(cwb::creditsText(1), QStringLiteral("0.00000001"));
  QCOMPARE(cwb::creditsText(100000000), QStringLiteral("1.00000000"));
  QCOMPARE(cwb::creditsText(5000000), QStringLiteral("0.05000000"));
  QCOMPARE(cwb::creditsText(123456789012LL),
           QStringLiteral("1234.56789012"));
  QCOMPARE(cwb::creditsText(-150000000), QStringLiteral("-1.50000000"));
}

void TestCredits::parseExact() {
  quint64 u = 0;
  QVERIFY(cwb::parseCredits(u"5", u));
  QCOMPARE(u, Q_UINT64_C(500000000));
  QVERIFY(cwb::parseCredits(u"0.01", u));
  QCOMPARE(u, Q_UINT64_C(1000000));
  QVERIFY(cwb::parseCredits(u".5", u));
  QCOMPARE(u, Q_UINT64_C(50000000));
  QVERIFY(cwb::parseCredits(u"1.00000001", u));
  QCOMPARE(u, Q_UINT64_C(100000001));
  QVERIFY(cwb::parseCredits(u"  2.5  ", u));  // trimmed
  QCOMPARE(u, Q_UINT64_C(250000000));
  QVERIFY(cwb::parseCredits(u"0", u));
  QCOMPARE(u, Q_UINT64_C(0));
  // The full uint64 range, exactly (a double parse would corrupt this).
  QVERIFY(cwb::parseCredits(u"184467440737.09551615", u));
  QCOMPARE(u, Q_UINT64_C(18446744073709551615));
  // Round-trip: text -> parse -> same units.
  QVERIFY(cwb::parseCredits(cwb::creditsText(123456789012LL), u));
  QCOMPARE(u, Q_UINT64_C(123456789012));
}

void TestCredits::parseRejects() {
  quint64 u = 0;
  QVERIFY(!cwb::parseCredits(u"", u));
  QVERIFY(!cwb::parseCredits(u".", u));
  QVERIFY(!cwb::parseCredits(u"-1", u));
  QVERIFY(!cwb::parseCredits(u"1.2.3", u));
  QVERIFY(!cwb::parseCredits(u"abc", u));
  QVERIFY(!cwb::parseCredits(u"1e8", u));
  QVERIFY(!cwb::parseCredits(u"1.000000001", u));  // 9 decimals
  QVERIFY(!cwb::parseCredits(u"184467440737.09551616", u));  // overflow
  QVERIFY(!cwb::parseCredits(u"99999999999999999999", u));   // overflow
}

QTEST_APPLESS_MAIN(TestCredits)
#include "test_credits.moc"
