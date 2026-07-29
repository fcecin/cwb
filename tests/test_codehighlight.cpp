#include "codehighlight.h"

#include <QtTest>

class TestCodeHighlight : public QObject {
  Q_OBJECT
 private slots:
  void luaTokens();
  void commentsPerLanguage();
  void escapingIsSafe();
  void pageHasLineNumbers();
};

void TestCodeHighlight::luaTokens() {
  const QString h =
      cwb::highlightCode("local x = 42 -- note\nreturn \"hi\"", "lua");
  QVERIFY(h.contains("<span class=kw>local</span>"));
  QVERIFY(h.contains("<span class=kw>return</span>"));
  QVERIFY(h.contains("<span class=nu>42</span>"));
  QVERIFY(h.contains("<span class=cm>-- note</span>"));
  QVERIFY(h.contains("<span class=st>\"hi\"</span>"));
  QVERIFY(!h.contains("<span class=kw>x</span>"));  // not a keyword
}

void TestCodeHighlight::commentsPerLanguage() {
  // '#' is a comment in shell, not in C.
  QVERIFY(cwb::highlightCode("# hi", "sh").contains("<span class=cm># hi</span>"));
  QVERIFY(!cwb::highlightCode("# hi", "c").contains("<span class=cm>"));
  // C block comment.
  QVERIFY(cwb::highlightCode("/* x */", "c").contains("<span class=cm>/* x */</span>"));
  // Lua has no C block comment; '/*' stays plain.
  QVERIFY(!cwb::highlightCode("/* x */", "lua").contains("<span class=cm>"));
}

void TestCodeHighlight::escapingIsSafe() {
  const QString h = cwb::highlightCode("a < b && c > d", "c");
  QVERIFY(h.contains("&lt;"));
  QVERIFY(h.contains("&gt;"));
  QVERIFY(h.contains("&amp;"));
  // The only '<' characters in the output belong to our own <span> tags.
  QVERIFY(!h.contains("a < b"));
}

void TestCodeHighlight::pageHasLineNumbers() {
  const QString h = cwb::codePageHtml("local a\nlocal b\nlocal c", "lua");
  QVERIFY(h.contains("class=gut"));
  QVERIFY(h.contains(">1\n2\n3<"));  // gutter numbers, one per source line
  QVERIFY(h.contains("<span class=kw>local</span>"));  // code still highlighted
}

QTEST_MAIN(TestCodeHighlight)
#include "test_codehighlight.moc"
