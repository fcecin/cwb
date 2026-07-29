#include "csvtable.h"

#include <QtTest>

class TestCsvTable : public QObject {
  Q_OBJECT
 private slots:
  void basicTable();
  void quotedFields();
  void escapingIsSafe();
  void tsv();
};

void TestCsvTable::basicTable() {
  const QString h = cwb::csvToHtmlPage("a,b,c\n1,2,3\n4,5,6\n");
  QVERIFY(h.contains("<th>a</th>"));
  QVERIFY(h.contains("<th>c</th>"));
  QVERIFY(h.contains("<td>1</td>"));
  QVERIFY(h.contains("<td>6</td>"));
  QVERIFY(!h.contains("<td>a</td>"));  // header not repeated as data
}

void TestCsvTable::quotedFields() {
  // a quoted field containing the separator, and a "" escaped quote
  const QString h =
      cwb::csvToHtmlPage("name,note\n\"Doe, J\",\"say \"\"hi\"\"\"\n");
  QVERIFY(h.contains("<td>Doe, J</td>"));      // comma inside quotes kept
  QVERIFY(h.contains("<td>say \"hi\"</td>"));  // "" -> literal quote
}

void TestCsvTable::escapingIsSafe() {
  const QString h = cwb::csvToHtmlPage("x\n<b>&\n");
  QVERIFY(h.contains("<td>&lt;b&gt;&amp;</td>"));
}

void TestCsvTable::tsv() {
  const QString h = cwb::csvToHtmlPage("a\tb\n1\t2\n", QLatin1Char('\t'));
  QVERIFY(h.contains("<th>a</th>"));
  QVERIFY(h.contains("<th>b</th>"));
  QVERIFY(h.contains("<td>2</td>"));
}

QTEST_MAIN(TestCsvTable)
#include "test_csvtable.moc"
