#include "cesurl.h"

#include <QtTest>

class TestCesUrl : public QObject {
  Q_OBJECT
 private slots:
  void luaWithPid();
  void luarpcNoPid();
  void fileScheme();
  void mainScheme();
  void emptyPathBecomesRoot();
  void invalid_data();
  void invalid();
  void link();
  void fileIndexDefaulting();
  void defaultPorts();
};

void TestCesUrl::defaultPorts() {
  // No port in the address: the scheme picks its convention.
  QCOMPARE(parseCesUrl("ces://node.example/").port, kCesMainPort);
  QCOMPARE(parseCesUrl("file://node.example/s/index.html").port, kCesRpcPort);
  QCOMPARE(parseCesUrl("compute://node.example/s/dice.lua").port, kCesRpcPort);
  QCOMPARE(parseCesUrl("lua://3@node.example/").port, kCesRpcPort);
  // luarpc has no default (each instance leases its own port): portless stays
  // port 0, the browser's "list this server's dialable instances" sentinel.
  const CesUrl lr = parseCesUrl("luarpc://node.example/");
  QVERIFY(lr.valid);
  QCOMPARE(lr.port, quint16(0));
  // An explicit port always wins.
  QCOMPARE(parseCesUrl("file://node.example:28831/s/x").port, quint16(28831));
  QCOMPARE(parseCesUrl("ces://node.example:28830/").port, quint16(28830));
}

void TestCesUrl::fileIndexDefaulting() {
  QCOMPARE(normalizeFileUrlPath(""), QStringLiteral("/"));
  QCOMPARE(normalizeFileUrlPath("/"), QStringLiteral("/"));
  QCOMPARE(normalizeFileUrlPath("/s/"), QStringLiteral("/s/index.html"));
  QCOMPARE(normalizeFileUrlPath("/p/site/"),
           QStringLiteral("/p/site/index.html"));
  QCOMPARE(normalizeFileUrlPath("/s/index.html"),
           QStringLiteral("/s/index.html"));  // exact paths pass through
  QCOMPARE(normalizeFileUrlPath("/p/a.txt"), QStringLiteral("/p/a.txt"));
}

void TestCesUrl::luaWithPid() {
  const CesUrl u = parseCesUrl("lua://5@localhost:28831/foo");
  QVERIFY(u.valid);
  QCOMPARE(u.scheme, QStringLiteral("lua"));
  QCOMPARE(u.mount, QStringLiteral("/ces/lua/1"));
  QCOMPARE(u.host, QStringLiteral("localhost"));
  QCOMPARE(u.port, quint16(28831));
  QCOMPARE(u.pid, quint64(5));
  QCOMPARE(u.path, QStringLiteral("/foo"));
  QVERIFY(!u.isMain);
}

void TestCesUrl::luarpcNoPid() {
  const CesUrl u = parseCesUrl("luarpc://localhost:40001/");
  QVERIFY(u.valid);
  QCOMPARE(u.scheme, QStringLiteral("luarpc"));
  QCOMPARE(u.mount, QStringLiteral("/ces/luarpc/1"));
  QCOMPARE(u.port, quint16(40001));
  QCOMPARE(u.pid, quint64(0));  // no userinfo -> no pid; the port IS the instance
}

void TestCesUrl::fileScheme() {
  const CesUrl u = parseCesUrl("file://node.example:28831/p/index.html");
  QVERIFY(u.valid);
  QCOMPARE(u.scheme, QStringLiteral("file"));
  QCOMPARE(u.mount, QStringLiteral("/ces/file/1"));
  QCOMPARE(u.path, QStringLiteral("/p/index.html"));
  QVERIFY(!u.isMain);
}

void TestCesUrl::mainScheme() {
  const CesUrl u = parseCesUrl("ces://node.example:53830/");
  QVERIFY(u.valid);
  QVERIFY(u.isMain);
  QVERIFY(u.mount.isEmpty());
  QCOMPARE(u.port, quint16(53830));
  QCOMPARE(u.path, QStringLiteral("/"));
  QCOMPARE(parseCesUrl("ces://node.example/account").path,
           QStringLiteral("/account"));
  QCOMPARE(parseCesUrl("ces://node.example/apps").path,
           QStringLiteral("/apps"));
}

void TestCesUrl::emptyPathBecomesRoot() {
  const CesUrl u = parseCesUrl("lua://1@h:1");
  QVERIFY(u.valid);
  QCOMPARE(u.path, QStringLiteral("/"));
}

void TestCesUrl::invalid_data() {
  QTest::addColumn<QString>("text");
  QTest::newRow("empty") << QString("");
  QTest::newRow("garbage") << QString("not a url");
  QTest::newRow("no-host") << QString("lua:///p");
  QTest::newRow("no-scheme") << QString("//host/p");
}

void TestCesUrl::invalid() {
  QFETCH(QString, text);
  QVERIFY(!parseCesUrl(text).valid);
}

void TestCesUrl::link() {
  auto nav = [](const LinkTarget& t, const QString& u) {
    QCOMPARE(int(t.kind), int(LinkTarget::Navigate));
    QCOMPARE(t.url, u);
  };
  // Relative links resolve against the page, keeping scheme + selector.
  nav(resolveLink("catalog", "file", "", "h", 9999, "/s/welcome"),
      "file://h:9999/s/welcome/catalog");
  nav(resolveLink("/s/index.html", "file", "", "h", 9999, "/s/welcome"),
      "file://h:9999/s/index.html");
  nav(resolveLink("../about", "file", "", "h", 9999, "/s/welcome"),
      "file://h:9999/s/about");
  nav(resolveLink("page2", "lua", "5", "h", 28831, "/app"),
      "lua://5@h:28831/app/page2");  // stays lua://, same pid
  // A full CES address navigates as-is.
  nav(resolveLink("luarpc://x:40001/y", "file", "", "h", 9999, "/s"),
      "luarpc://x:40001/y");
  // Real-web urls escape to the OS browser; fragments/empty do nothing.
  QCOMPARE(int(resolveLink("http://example.com/z", "file", "", "h", 9999, "/s").kind),
           int(LinkTarget::Browser));
  QCOMPARE(int(resolveLink("https://e.com", "file", "", "h", 9999, "/s").kind),
           int(LinkTarget::Browser));
  QCOMPARE(int(resolveLink("#x", "file", "", "h", 9999, "/s").kind),
           int(LinkTarget::None));
  QCOMPARE(int(resolveLink("", "file", "", "h", 9999, "/s").kind),
           int(LinkTarget::None));
}

QTEST_MAIN(TestCesUrl)
#include "test_cesurl.moc"
