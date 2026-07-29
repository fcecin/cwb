#include "worktree.h"

#include <QTemporaryDir>
#include <QtTest>

class TestWorktree : public QObject {
 Q_OBJECT
  QTemporaryDir dataDir_;
 private slots:
  void initTestCase() {
    QVERIFY(dataDir_.isValid());
    qputenv("XDG_DATA_HOME", dataDir_.path().toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("ces"));
    QCoreApplication::setApplicationName(QStringLiteral("cwb-worktree-test"));
    QDir(cwb::workRoot()).removeRecursively();
  }
  void saveAndReadBack();
  void remotesAccumulate();
  void listByPrefix();
  void rejectsBadPaths();
  void crashSafeDraft();
  void retiredRoundTrip();
  void deleteRemovesAll();
};

void TestWorktree::saveAndReadBack() {
  const QString zp = QStringLiteral("/h/abc123/posts/first.html");
  QVERIFY(cwb::workSave(zp, "<html>art</html>", "Title\n\nbody", "Title"));
  QCOMPARE(cwb::workArtifact(zp), QByteArray("<html>art</html>"));
  // Re-save overwrites (one slug, one work).
  QVERIFY(cwb::workSave(zp, "<html>v2</html>", "Title\n\nbody2", "Title"));
  QCOMPARE(cwb::workArtifact(zp), QByteArray("<html>v2</html>"));
  // The source rides alongside.
  QFile src(cwb::workLocalPath(zp) + ".src.txt");
  QVERIFY(src.open(QIODevice::ReadOnly));
  QCOMPARE(src.readAll(), QByteArray("Title\n\nbody2"));
}

void TestWorktree::remotesAccumulate() {
  const QString zp = QStringLiteral("/h/abc123/posts/first.html");
  cwb::workNoteRemote(zp, QStringLiteral("srv-a:1"));
  cwb::workNoteRemote(zp, QStringLiteral("srv-b:2"));
  cwb::workNoteRemote(zp, QStringLiteral("srv-a:1"));  // idempotent
  QCOMPARE(cwb::workRemotes(zp),
           QStringList({QStringLiteral("srv-a:1"), QStringLiteral("srv-b:2")}));
  // A re-save keeps the remotes (provenance survives republish).
  QVERIFY(cwb::workSave(zp, "<html>v3</html>", "s", "Title"));
  QCOMPARE(cwb::workRemotes(zp).size(), 2);
}

void TestWorktree::listByPrefix() {
  cwb::workSave(QStringLiteral("/h/abc123/posts/second.html"), "x", "s", "S");
  cwb::workSave(QStringLiteral("/h/OTHER/posts/foreign.html"), "y", "s", "F");
  const QStringList mine = cwb::workList(QStringLiteral("/h/abc123/posts/"));
  QCOMPARE(mine.size(), 2);  // first + second; sidecars and sources excluded
  QVERIFY(mine.contains(QStringLiteral("/h/abc123/posts/first.html")));
  QVERIFY(mine.contains(QStringLiteral("/h/abc123/posts/second.html")));
}

void TestWorktree::rejectsBadPaths() {
  QVERIFY(cwb::workLocalPath(QStringLiteral("h/no-slash")).isEmpty());
  QVERIFY(cwb::workLocalPath(QStringLiteral("/h/../etc/passwd")).isEmpty());
  QVERIFY(!cwb::workSave(QStringLiteral("relative"), "a", "s", "t"));
}

void TestWorktree::crashSafeDraft() {
  const QString key = QStringLiteral("server:53831|writer|vellum-test");
  QVERIFY(cwb::draftRemove(key));
  QVERIFY(cwb::draftSave(key, QStringLiteral("A title"),
                         QStringLiteral("first\n\nsecond")));
  QString title, body;
  QVERIFY(cwb::draftLoad(key, title, body));
  QCOMPARE(title, QStringLiteral("A title"));
  QCOMPARE(body, QStringLiteral("first\n\nsecond"));
  QVERIFY(cwb::draftSave(key, QStringLiteral("Short"), QStringLiteral("x")));
  QVERIFY(cwb::draftLoad(key, title, body));
  QCOMPARE(title, QStringLiteral("Short"));
  QCOMPARE(body, QStringLiteral("x"));
  QVERIFY(cwb::draftRemove(key));
  QVERIFY(!cwb::draftLoad(key, title, body));
}

void TestWorktree::retiredRoundTrip() {
  const QString zp = QStringLiteral("/h/abc123/posts/retire-me.html");
  QVERIFY(cwb::workSave(zp, "<html>r</html>", "s", "Retire me"));
  QVERIFY(!cwb::workRetired(zp));
  cwb::workSetRetired(zp, true);
  QVERIFY(cwb::workRetired(zp));
  // Retiring keeps provenance intact.
  QCOMPARE(cwb::workTitle(zp), QStringLiteral("Retire me"));
  // Publishing again (a re-save) is the un-retire gesture.
  QVERIFY(cwb::workSave(zp, "<html>r2</html>", "s", "Retire me"));
  QVERIFY(!cwb::workRetired(zp));
  // A work that does not exist is never retired and cannot be marked.
  cwb::workSetRetired(QStringLiteral("/h/abc123/posts/ghost.html"), true);
  QVERIFY(!cwb::workRetired(QStringLiteral("/h/abc123/posts/ghost.html")));
}

void TestWorktree::deleteRemovesAll() {
  const QString zp = QStringLiteral("/h/abc123/posts/doomed.html");
  QVERIFY(cwb::workSave(zp, "<html>d</html>", "src", "Doomed"));
  cwb::workNoteRemote(zp, QStringLiteral("srv-a:1"));
  const QString local = cwb::workLocalPath(zp);
  QVERIFY(QFile::exists(local));
  QVERIFY(QFile::exists(local + ".src.txt"));
  QVERIFY(QFile::exists(local + ".work.toml"));
  QVERIFY(cwb::workDelete(zp));
  QVERIFY(!QFile::exists(local));
  QVERIFY(!QFile::exists(local + ".src.txt"));
  QVERIFY(!QFile::exists(local + ".work.toml"));
  QVERIFY(cwb::workArtifact(zp).isEmpty());
  // Deleting the already-deleted is fine (idempotent).
  QVERIFY(cwb::workDelete(zp));
}

QTEST_APPLESS_MAIN(TestWorktree)
#include "test_worktree.moc"
